#include "sender.h"
#include "globals.h"

#include <nlohmann/json_fwd.hpp>
#include <utility>

using namespace rtc;
using namespace std;

using json = nlohmann::json;

Sender::Sender(const queue<string> &files, const string &base_dir,
               const shared_ptr<DataChannel> &data_channel,
               bool is_encrypted, const string &password)
        : pending_files_(files), base_directory_(base_dir), data_channel_(data_channel) {

    total_files_in_batch_ = pending_files_.size();

    current_filepath_ = pending_files_.front();
    pending_files_.pop();

    string safe_read_path = file_helper::to_windows_long_path(current_filepath_);

    infile_.open(safe_read_path , ios::binary);
    if (!infile_.is_open()) {
        throw runtime_error("Fatal Error : Cannot open the file ->" + safe_read_path);
    }

    vector<uint8_t> salt;
    vector<uint8_t> master_iv;
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

        if (!raft_globals::is_running) return;

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
                                raft_globals::shutdown("Cannot open next file in batch: " + safe_read_path);
                                current_state_ = SenderState::DONE;
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
                            raft_globals::shutdown("Batch transfer complete! All files sent successfully.");
                            current_state_ = SenderState::DONE;
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
                                raft_globals::shutdown("Cannot open next file in batch: " + safe_read_path);
                                current_state_ = SenderState::DONE;
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
                            current_state_ = SenderState::WAITING_META_ACK;
                            data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));

                        } else {
                            // The queue is empty.
                            raft_globals::shutdown("Batch transfer complete! All files sent successfully.");
                            current_state_ = SenderState::DONE;
                        }
                    }
                }
            }
        }
    });

    data_channel_->setBufferedAmountLowThreshold(128 * 1024); // 128KiB

    data_channel_->onBufferedAmountLow([this]() {
        if (current_state_ != SenderState::TRANSFERRING || !raft_globals::is_running) return;

        try {
            flush_network_queue();
            queue_cv_.notify_one();
        }catch (exception& e) {
            raft_globals::shutdown(string("Network buffer callback crashed : ") + e.what());
        }
    });

    //--kick-starting transfer--
    current_state_ = SenderState::WAITING_MANIFEST_ACK;
    data_channel_->send(reinterpret_cast<const std::byte*>(&data_manifest_), sizeof(DataManifest));
}
