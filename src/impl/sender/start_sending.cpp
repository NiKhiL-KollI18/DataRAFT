#include "sender.h"
#include "globals.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;
using ui = UIManager;

void Sender::start_sending() {
    ui::log_internals("[Sender] Arming network callbacks and initializing handshake...");

    //--IMPLICIT STATE MACHINE--
    data_channel_->onMessage([this](const variant<binary , string> &data) {

        if (!raft_globals::is_running) return;

        if (holds_alternative<binary>(data)) {
            const auto& raw_data = get<binary>(data);

            if (raw_data.size() == sizeof(TransferAck)) {
                TransferAck ack{};
                memcpy(&ack , raw_data.data() , sizeof(TransferAck));

                if (current_state_ == SenderState::WAITING_MANIFEST_ACK) {
                    if (!ack.accept_transfer_) {
                        ui::log_internals("[Sender] Receiver rejected the manifest. Aborting.");
                        current_state_ = SenderState::DONE;
                        return;
                    }
                    ui::log_internals("[Sender] Manifest accepted , Sending file metadata...");

                    current_state_ = SenderState::WAITING_META_ACK;

                    data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));
                }
                else if (current_state_ == SenderState::WAITING_META_ACK) {
                    if (!ack.accept_transfer_) {
                        ui::log_internals("[Sender] Receiver rejected the metadata/file. Aborting.");
                        current_state_ = SenderState::DONE;
                        return;
                    }

                    //--SKIP LOGIC--
                    if (ack.resume_from_block_ == ULLONG_MAX) {
                        ui::log_internals("[Sender] Receiver requested to skip the file. Skipping...");
                        global_bytes_transferred_ += metadata_.file_size_;
                        infile_.close();

                        // Execute Batch Loop
                        if (!pending_files_.empty()) {
                            current_filepath_ = pending_files_.front();
                            pending_files_.pop();

                            string safe_read_path = file_helper::to_windows_long_path(current_filepath_);
                            infile_.open(safe_read_path, ios::binary);
                            if (!infile_.is_open()) {
                                raft_globals::shutdown(Level::ERROR , "Cannot open next file in batch: " + safe_read_path);
                                current_state_ = SenderState::DONE;
                                return;
                            }

                            memset(&metadata_, 0, sizeof(FileMeta));
                            file_helper::extract_metadata(current_filepath_, base_directory_, metadata_);

                            if (data_manifest_.is_encrypted_) {
                                auto new_master_iv = encryptor_->generate_new_master_iv();
                                memcpy(metadata_.master_crypto_iv_, new_master_iv.data(), 12);
                            }
                            ui::log_internals("[Sender] Loading next file in batch: " + current_filepath_);

                            data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));
                        }else {
                            if (data_manifest_.is_batch_directory_) {
                                ui::print(Level::SUCCESS , "Batch transfer complete! All files sent successfully.");
                            }else {
                                ui::print(Level::SUCCESS , "File transferred successfully.");
                            }
                            raft_globals::shutdown(Level::SYSTEM , "");
                            current_state_ = SenderState::DONE;
                        }
                        return;
                    }

                    //--NORMAL TRANSFER
                    resume_from_block_ = ack.resume_from_block_;
                    ui::log_internals( "[Sender] Metadata accepted. Sending from byte : "
                        + to_string(resume_from_block_ ));

                    current_state_ = SenderState::TRANSFERRING;

                    producer_thread_ = std::thread(&Sender::producer , this);
                }
                else if (current_state_ == SenderState::TRANSFERRING) {
                    if (ack.accept_transfer_) {
                        ui::log_internals("[Sender] File completed successfully!");

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
                                raft_globals::shutdown(Level::ERROR , "Cannot open next file in batch: " + safe_read_path);
                                current_state_ = SenderState::DONE;
                                return;
                            }

                            // 2. Extract new metadata (using a normal path)
                            memset(&metadata_, 0, sizeof(FileMeta));
                            file_helper::extract_metadata(current_filepath_, base_directory_, metadata_);

                            if (data_manifest_.is_encrypted_) {
                                auto new_master_iv = encryptor_->generate_new_master_iv();
                                memcpy(metadata_.master_crypto_iv_, new_master_iv.data(), 12);
                            }

                            ui::log_internals("[Sender] Loading next file in batch: " + current_filepath_);
                            current_state_ = SenderState::WAITING_META_ACK;
                            data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));

                        } else {
                            // The queue is empty.
                            if (data_manifest_.is_batch_directory_) {
                                ui::print(Level::SUCCESS , "Batch transfer complete! All files sent successfully.");
                            }else {
                                ui::print(Level::SUCCESS , "File transferred successfully.");
                            }
                            raft_globals::shutdown(Level::SYSTEM , "");
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
            raft_globals::shutdown(Level::ERROR , string("Network buffer callback crashed : ") + e.what());
        }
    });

    //--kick-starting transfer--
    current_state_ = SenderState::WAITING_MANIFEST_ACK;
    data_channel_->send(reinterpret_cast<const std::byte*>(&data_manifest_), sizeof(DataManifest));
}