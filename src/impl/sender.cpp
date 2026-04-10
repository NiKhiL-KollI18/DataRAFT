#include "sender.h"

#include <nlohmann/json_fwd.hpp>

using namespace rtc;
using namespace std;

using json = nlohmann::json;

Sender::Sender(const string &filepath, const shared_ptr<DataChannel> &data_channel,
    bool is_encrypted, const string &password , function<void()> on_complete)
        : filepath_(filepath) , password_(password) , data_channel_(data_channel) , on_transfer_complete_(on_complete){

    infile_.open(filepath_ , ios::binary);
    if (!infile_.is_open()) {
        throw runtime_error("Fatal Error : Cannot open the file ->" + filepath_);
    }

    hasher_.emplace();

    vector<unsigned char> salt;
    vector<unsigned char> iv;
    string pass_hash = "";

    if (is_encrypted) {
        encryptor_.emplace(password_);
        auto crypto_params = encryptor_->get_crypto_params();
        salt = crypto_params.salt;
        iv = crypto_params.iv;

        pass_hash = encryptor_->get_password_hash();
    }

    file_helper::extract_metadata(filepath_ , filepath_ ,metadata_, "", false);

    if (metadata_.is_compressed_) {
        compressor_.emplace();
    }

    file_helper::create_data_manifest(data_manifest_ , filepath_ , is_encrypted, pass_hash , salt , iv);
}

Sender::~Sender() {
    is_file_reading_completed_ = true;
    queue_cv_.notify_all();

    if (producer_thread_.joinable()) {
        producer_thread_.join();
    }
    if (infile_.is_open()) {
        infile_.close();
    }
}

void Sender::start_sending() {
    cout << "[Sender] Arming network callbacks and initializing handshake..." << endl;

    //--IMPLICIT STATE MACHINE--
    data_channel_->onMessage([this](variant<binary , string> data) {
        if (holds_alternative<binary>(data)) {
            auto raw_data = get<binary>(data);

            if (raw_data.size() == sizeof(TransferAck)) {
                TransferAck ack;
                memcpy(&ack , raw_data.data() , sizeof(TransferAck));

                if (current_state_ == SenderState::WAITING_MANIFEST_ACK) {
                    if (!ack.accept_transfer_) {
                        cout << "[Sender] Receiver rejected the manifest. Aborting." << endl;
                        current_state_ = SenderState::DONE;
                        return;
                    }

                    cout << "[Sender] Manifest accepted , Sending file metadata..." << endl;

                    current_state_ = SenderState::WAITING_META_ACK;

                    data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));
                }
                else if (current_state_ == SenderState::WAITING_META_ACK) {
                    if (!ack.accept_transfer_) {
                        // THE FIX: Updated the logging to correctly reflect the metadata rejection
                        cout << "[Sender] Receiver rejected the metadata/file. Aborting." << endl;
                        current_state_ = SenderState::DONE;
                        return;
                    }

                    resume_from_byte = ack.resume_from_byte_;
                    cout << "[Sender] Metadata accepted. Sending from byte : " << resume_from_byte << endl;

                    current_state_ = SenderState::TRANSFERRING;

                    producer_thread_ = std::thread(&Sender::producer , this);
                }
                else if (current_state_ == SenderState::TRANSFERRING) {
                    if (ack.accept_transfer_) {
                        cout << "[Sender] Final ACK received! Transfer is completed successfully!" << endl;
                        if (on_transfer_complete_) {
                            on_transfer_complete_();
                        }
                    }
                    else {
                        cout << "[Sender] Final ACK rejected! Receiver reported a checksum or decryption failure." << endl;
                        if (on_transfer_complete_) {
                            on_transfer_complete_();
                        }
                    }
                    current_state_ = SenderState::DONE;
                }
            }
        }
    });

    data_channel_->setBufferedAmountLowThreshold(128 * 1024); // 128KiB

    data_channel_->onBufferedAmountLow([this]() {
        if (current_state_ != SenderState::TRANSFERRING) return;

        try {
            while (true) {
                std::vector<char> chunk_to_send;


                {
                    lock_guard<mutex> lock(queue_mutex_);
                    if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                        break;
                    }
                    chunk_to_send = std::move(chunk_queue_.front());
                    chunk_queue_.pop();
                    current_queue_size_ -= chunk_to_send.size();
                }

                data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()), chunk_to_send.size());
            }

            queue_cv_.notify_one();

        } catch (const std::exception& e) {
            cout << "\n[Fatal Error] Network buffer callback crashed: " << e.what() << endl;
            if (on_transfer_complete_) on_transfer_complete_();
        }
    });

    //--kick-starting transfer--
    current_state_ = SenderState::WAITING_MANIFEST_ACK;

    // True Zero-Copy send
    data_channel_->send(reinterpret_cast<const std::byte*>(&data_manifest_), sizeof(DataManifest));
}

void Sender::producer() {
    cout << "[Sender] producer thread is active. Reading from disk..." << endl;

    try {
        if (resume_from_byte > 0) {
            infile_.seekg(resume_from_byte , ios::beg);
        }

        while (infile_ && current_state_ == SenderState::TRANSFERRING) {
            vector<char> buffer(BUCKET_SIZE);

            infile_.read(buffer.data(), BUCKET_SIZE);
            streamsize bytes_read = infile_.gcount();

            if (bytes_read == 0) break;

            buffer.resize(bytes_read);
            bool is_last_chunk = (infile_.peek() == EOF || infile_.eof());

            hasher_->update_hash(buffer);

            if (compressor_) {
                compressor_->compress_chunk(buffer , is_last_chunk);
            }

            if (encryptor_) {
                encryptor_->encrypt_chunk(buffer);
            }

            unique_lock<mutex> lock(queue_mutex_);

            queue_cv_.wait(lock , [this]() {
                return current_queue_size_ < MAX_QUEUE_SIZE;
            });

            size_t final_size = buffer.size();
            chunk_queue_.push(std::move(buffer));
            current_queue_size_ += final_size;

            // UNLOCK EXPLICITLY BEFORE NETWORK CALLS
            lock.unlock();

            // STALL PREVENTION (Safe version)
            while (true) {
                std::vector<char> chunk_to_send;
                {
                    lock_guard<mutex> inner_lock(queue_mutex_);
                    if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                        break;
                    }
                    chunk_to_send = std::move(chunk_queue_.front());
                    chunk_queue_.pop();
                    current_queue_size_ -= chunk_to_send.size();
                } // Unlocked!

                // Send outside the lock
                data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()), chunk_to_send.size());
            }
        }

        // End of File : Create Receipt Metadata
        cout << "[Sender] Disk read completed. Generating Cryptographic receipt..." << endl;

        FileMeta dummy_meta{};
        memset(&dummy_meta , 0 , sizeof(FileMeta));

        dummy_meta.is_transfer_complete_ = true;
        strncpy(dummy_meta.checksum_sha256_ , hasher_->get_sha256_hash().c_str() , sizeof(dummy_meta.checksum_sha256_) - 1);

        if (encryptor_) {
            vector<unsigned char> auth_tag = encryptor_->get_auth_tag();
            memcpy(dummy_meta.tag , auth_tag.data(), min(sizeof(dummy_meta.tag), auth_tag.size()));
        }

        vector<char> dummy_metadata(sizeof(FileMeta));
        memcpy(dummy_metadata.data(), &dummy_meta, sizeof(FileMeta));

        // Safely push the final dummy metadata
        {
            lock_guard<mutex> lock(queue_mutex_);
            size_t dummy_size = dummy_metadata.size();
            chunk_queue_.push(std::move(dummy_metadata));
            current_queue_size_ += dummy_size;
        } // Unlocked!

        // Safely flush the final queue
        while (true) {
            std::vector<char> chunk_to_send;
            {
                lock_guard<mutex> inner_lock(queue_mutex_);
                if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                    break;
                }
                chunk_to_send = std::move(chunk_queue_.front());
                chunk_queue_.pop();
                current_queue_size_ -= chunk_to_send.size();
            }
            data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()), chunk_to_send.size());
        }

        is_file_reading_completed_ = true;
        cout << "[Sender] pipeline completed. Awaiting final ACK from receiver" << endl;

    } catch (const std::exception& e) {
        cout << "\n[Fatal Thread Error] Producer crashed: " << e.what() << endl;
        if (on_transfer_complete_) on_transfer_complete_();
    }
}