#include "sender.h"

#include <nlohmann/json_fwd.hpp>
#include <utility>

using namespace rtc;
using namespace std;

using json = nlohmann::json;

Sender::Sender(const queue<string> &files, const string &base_dir,
               const shared_ptr<DataChannel> &data_channel,
               bool is_encrypted, const string &password , function<void()> on_complete)
        : pending_files_(files), base_directory_(base_dir), data_channel_(data_channel) , on_transfer_complete_(on_complete){

    total_files_in_batch_ = pending_files_.size();

    //pop the first file to initialize the pipeline
    current_filepath_ = pending_files_.front();
    pending_files_.pop();

    string safe_read_path = file_helper::to_windows_long_path(current_filepath_);

    infile_.open(safe_read_path , ios::binary);
    if (!infile_.is_open()) {
        throw runtime_error("Fatal Error : Cannot open the file ->" + safe_read_path);
    }

    vector<unsigned char> salt;
    vector<unsigned char> master_iv;
    string pass_hash = "";

    file_helper::extract_metadata(current_filepath_ , base_directory_ , metadata_);

    if (is_encrypted) {
        encryptor_.emplace(password);
        password_ = password;

        salt = encryptor_->get_salt();
        pass_hash = encryptor_->get_password_hash();

        master_iv = encryptor_->generate_new_master_iv();

        memcpy(metadata_.master_crypto_iv_ , master_iv.data() , 12);

    }

    memset(&data_manifest_, 0, sizeof(DataManifest));

    file_helper::create_data_manifest(data_manifest_ , current_filepath_ , is_encrypted, pass_hash , salt);

    data_manifest_.total_file_count_ = total_files_in_batch_;
    data_manifest_.is_batch_directory_ = (total_files_in_batch_ > 1);
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

                    //--SKIP LOGIC--
                    if (ack.resume_from_block_ == ULLONG_MAX) {
                        cout << "[Sender] SKIPPING..." << endl;
                        infile_.close();

                        // Execute Batch Loop
                        if (!pending_files_.empty()) {
                            current_filepath_ = pending_files_.front();
                            pending_files_.pop();

                            string safe_read_path = file_helper::to_windows_long_path(current_filepath_);
                            infile_.open(safe_read_path, ios::binary);
                            if (!infile_.is_open()) {
                                cout << "[Fatal] Cannot open next file in batch: " << safe_read_path << endl;
                                current_state_ = SenderState::DONE;
                                if (on_transfer_complete_) on_transfer_complete_();
                                return;
                            }

                            memset(&metadata_, 0, sizeof(FileMeta));
                            file_helper::extract_metadata(current_filepath_, base_directory_, metadata_);

                            if (data_manifest_.is_encrypted_) {
                                auto new_master_iv = encryptor_->generate_new_master_iv();
                                memcpy(metadata_.master_crypto_iv_, new_master_iv.data(), 12);
                            }
                            cout << "[Sender] Loading next file in batch: " << current_filepath_ << endl;

                            data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));
                        }else {
                            cout << "[Sender] Batch transfer complete! All files sent successfully." << endl;
                            current_state_ = SenderState::DONE;
                            if (on_transfer_complete_) on_transfer_complete_();
                        }
                        return;
                    }

                    //--NORMAL TRANSFER
                    resume_from_block_ = ack.resume_from_block_;
                    cout << "[Sender] Metadata accepted. Sending from byte : " << resume_from_block_ << endl;

                    current_state_ = SenderState::TRANSFERRING;

                    producer_thread_ = std::thread(&Sender::producer , this);
                }
                else if (current_state_ == SenderState::TRANSFERRING) {
                    if (ack.accept_transfer_) {
                        cout << "[Sender] File completed successfully!" << endl;

                        // Clean up the finished producer thread
                        if (producer_thread_.joinable()) {
                            producer_thread_.join();
                        }
                        infile_.close();

                        if (!pending_files_.empty()) {
                            //Load the next file
                            current_filepath_ = pending_files_.front();
                            pending_files_.pop();

                            string safe_read_path = file_helper::to_windows_long_path(current_filepath_);

                            infile_.open(safe_read_path, ios::binary);
                            if (!infile_.is_open()) {
                                cout << "[Fatal] Cannot open next file in batch: " << safe_read_path << endl;
                                current_state_ = SenderState::DONE;
                                if (on_transfer_complete_) on_transfer_complete_();
                                return;
                            }

                            // 2. Extract new metadata (using normal path)
                            memset(&metadata_, 0, sizeof(FileMeta));
                            file_helper::extract_metadata(current_filepath_, base_directory_, metadata_);

                            if (data_manifest_.is_encrypted_) {
                                auto new_master_iv = encryptor_->generate_new_master_iv();
                                memcpy(metadata_.master_crypto_iv_, new_master_iv.data(), 12);
                            }

                            cout << "[Sender] Loading next file in batch: " << current_filepath_ << endl;

                            // 3. Jump back to Metadata phase and send it!
                            current_state_ = SenderState::WAITING_META_ACK;
                            data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));

                        } else {
                            // The queue is empty.
                            cout << "[Sender] Batch transfer complete! All files sent successfully." << endl;
                            current_state_ = SenderState::DONE;
                            if (on_transfer_complete_) on_transfer_complete_();
                        }
                    }
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

