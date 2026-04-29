#include "receiver.h"
#include "webrtc_client.h"

#include <filesystem>

#include "sender.h"

using namespace std;
using namespace rtc;
namespace fs = std::filesystem;


FileReceiver::FileReceiver(shared_ptr<DataChannel> data_channel, string download_dir , bool skip_existing , function<void()> on_complete)
    :base_download_path_(std::move(download_dir)) , data_channel_(data_channel) , on_transfer_complete_(on_complete)
    , skip_existing_files_(skip_existing){
    memset(&manifest_ , 0 , sizeof(DataManifest));
    memset(&metadata_ , 0 , sizeof(FileMeta));
}

FileReceiver::~FileReceiver() {
    if (outfile_.is_open()) {
        outfile_.close();
    }
}

void FileReceiver::send_ack(bool accept, uint64_t resume_block) {
    TransferAck ack{};
    ack.accept_transfer_ = accept;
    ack.resume_from_block_ = resume_block;

    data_channel_->send(reinterpret_cast<const std::byte*>(&ack) , sizeof(TransferAck));
}

void FileReceiver::start_receiving() {
    cout << "[Receiver] listening for incoming transfers..." << endl;

    //---IMPLICIT STATE MACHINE---
    data_channel_->onMessage([this](variant<binary , string> data) {
        try {
            if (holds_alternative<binary>(data)) {

                auto raw_data = get<binary>(data);

                if (raw_data.empty()) return;

                switch (current_state_) {
                    case ReceiverState::AWAITING_MANIFEST:
                        process_manifest(raw_data);
                        break;

                    case ReceiverState::AWAITING_PASSWORD:
                        break;

                    case ReceiverState::AWAITING_METADATA:
                        process_metadata(raw_data);
                        break;

                    case ReceiverState::RECEIVING_DATA:
                    {
                        vector<char> chunk(reinterpret_cast<const char*>(raw_data.data()) ,
                            reinterpret_cast<const char*>(raw_data.data() + raw_data.size()));

                        //Extracting router flag
                        auto footer_flag = static_cast<PacketType>(chunk.back());
                        chunk.pop_back();

                        if (footer_flag == PacketType::RAW_DATA) {
                            //standard data packets
                            process_data_chunks(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::BLOCK_FOOTER) {
                            //Hit 8MB Block boundary! Verify this block's integrity
                            process_block_footer(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::FILE_EOF) {
                            //end of the entire file.
                            cout << "[Receiver] Found EOF footer" << endl;
                            process_file_eof();
                        }
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            cout << "\n[Fatal Error] Pipeline crashed: " << e.what() << endl;
            if (on_transfer_complete_) on_transfer_complete_();
        }
    });
}

void FileReceiver::process_manifest(const binary &data) {
    if (data.size() != sizeof(DataManifest)) {
        cout << "[Receiver] received invalid manifest size. aborting..." << endl;
        send_ack(false, 0);
        return;
    }

    memcpy(&manifest_ , data.data() , sizeof(DataManifest));

    cout << "\n INCOMING TRANSFER : " << "\n";
    cout << "Sender : " << manifest_.sender_name_ << "\n";
    cout << "batch Transfer : " << (manifest_.is_batch_directory_ ? "True" : "False") << "\n";
    cout << "Encrypted : " << (manifest_.is_encrypted_ ? "True" : "False") << endl;

    if (manifest_.is_encrypted_) {
        current_state_ = ReceiverState::AWAITING_PASSWORD;
        handle_password_auth(); //detached thread
    }
    else {
        current_state_ = ReceiverState::AWAITING_METADATA;
        send_ack(true, 0);
    }
}

void FileReceiver::handle_password_auth() {
    std::thread([this]() {
        string input_password;
        bool auth_success = false;

        vector<uint8_t> salt(manifest_.crypto_salt_ , manifest_.crypto_salt_ + 16);

        char safe_hash[128] = {0};
        memcpy(safe_hash , manifest_.password_hash_sha256_ , sizeof(manifest_.password_hash_sha256_));
        string expected_hash(safe_hash);

        while (!auth_success) {
            cout << "\n[Receiver] Transfer is locked. Please Enter Password : ";
            cin >> input_password;

            try {
                file_helper::StreamDecryptor temp_decrypt(input_password , salt);

                if (temp_decrypt.check_password_hash(expected_hash)) {
                    cout << "\n[Receiver] Password verified." << endl;
                    password_ = input_password;

                    decryptor_.emplace(input_password, salt);

                    auth_success = true;
                }else {
                    cout << "\r[Receiver] Incorrect password. Please try again." << flush;
                }
            }catch (const std::exception& e) {
                cout << "\r[Receiver] Cryptographic Error: " << e.what() << endl;
            }
        }
        send_ack(true , 0);
        current_state_ = ReceiverState::AWAITING_METADATA;
    }).detach();
}

void FileReceiver::process_metadata(const binary &data) {
    if (data.size() != sizeof(FileMeta)) {
        cout << "[Receiver] received invalid metadata size. aborting..." << endl;
        send_ack(false, 0);
        return;
    }

    memcpy(&metadata_ , data.data() , sizeof(FileMeta));

    // Calculate the target paths
    string raw_target_path = base_download_path_ + "/" + string(metadata_.relative_path_);
    string raw_raft_path = raw_target_path + ".raftpath";

    // Convert to a Windows Long Path
    final_filepath_ = file_helper::to_windows_long_path(raw_target_path);
    current_filepath_ = file_helper::to_windows_long_path(raw_raft_path); //Active downloads uses .raftpath

    try {
        fs::path target_path(current_filepath_);
        if (target_path.has_parent_path()) {
            fs::create_directories(target_path.parent_path());
        }
    } catch (const fs::filesystem_error& e) {
        cout << "[Fatal] Could not create directories for: " << current_filepath_ << "\n" << e.what() << endl;
        send_ack(false, 0);
        return;
    }

    //---1 . Check for complete files---
    if (fs::exists(final_filepath_)) {
        if (skip_existing_files_) {
            cout << "[Receiver] File Already exists. Skipping : " << metadata_.relative_path_ << endl;

            if (manifest_.is_batch_directory_ && current_file_count_ < manifest_.total_file_count_) {
                current_file_count_++;
            }
            //send special flag to indicate skip
            send_ack(true, UINT64_MAX);
            return;
        }
        else {
            cout << "[Receiver] Replace Enabled. Overwritting completed files..." << endl;
            fs::remove(final_filepath_);
        }
    }

    //---2. RESUME PARTIAL FILES---
    uint64_t resume_block = 0;

    if (fs::exists(current_filepath_)) {
        if (skip_existing_files_) {
            uint64_t current_disk_size = fs::file_size(current_filepath_);

            //drop incomplete blocks
            resume_block = current_disk_size / BLOCK_SIZE;
            uint64_t safe_byte_size = resume_block * BLOCK_SIZE;

            //snip the existing file to safe_byte_size
            fs::resize_file(current_filepath_ , safe_byte_size);

            cout << "[Receiver] Found partial file. Resuming from block" << resume_block << endl;
        }else {
            //replace mode : delete partial file
            fs::remove(current_filepath_);
        }
    }else {
        cout << "[Receiver] Starting new file download : " << metadata_.relative_path_ << endl;
    }

    //--3. Open file--
    //if resuming open in append mode, else truncate
    outfile_.open(current_filepath_ , ios::binary | (resume_block > 0 ? ios::app : ios::trunc));
    if (!outfile_.is_open()) {
        cout << "[Fatal] Cannot open file for writing : " << current_filepath_ << endl;
        send_ack(false, 0);
        return;
    }

    //sync state machine
    current_block_index_ = resume_block;
    bytes_processed_count_ = resume_block * BLOCK_SIZE;

    //--4. REFORMERS--
    hasher_.emplace();

    if (metadata_.is_compressed_) {
        decompressor_.emplace();
    }

    if (manifest_.is_encrypted_) {
        vector<uint8_t> master_iv(metadata_.master_crypto_iv_ , metadata_.master_crypto_iv_ + 12);
        auto block_iv = file_helper::derive_block_iv(master_iv , current_block_index_);

        decryptor_->init_new_block(block_iv);
    }

    //initializing data receiver
    send_ack(true , resume_block);
    current_state_ = ReceiverState::RECEIVING_DATA;
}

void FileReceiver::process_data_chunks(vector<char> &&chunk) {
    if (chunk.empty()) return;

    try {
        // 1. Decrypt
        if (decryptor_) decryptor_->decrypt_chunk(chunk);

        // 2.Decompress
        if (decompressor_) decompressor_->decompress_chunk(chunk);

        // 3.hash
        if (hasher_) hasher_->update_hash(chunk);

        outfile_.write(chunk.data(), chunk.size());
        bytes_processed_count_ += chunk.size();

        if (bytes_processed_count_ >= last_significant_point_size_ + 10 * 1024 * 1024) {
            cout << "\r Received : " << bytes_processed_count_ / (1024 * 1024) << "MB" << flush;
            last_significant_point_size_ = bytes_processed_count_;
        }

    } catch (const std::exception& e) {
        cout << "\n[Receiver] Pipeline Error during data processing : " << e.what() << endl;
    }
}

void FileReceiver::process_block_footer(vector<char> &&footer_data) {
    if (footer_data.size() != sizeof(BlockFooter)) {
        cout << "[Receiver] received corrupted block footer." << endl;
        return;
    }

    BlockFooter footer{};
    memcpy(&footer , footer_data.data() , sizeof(BlockFooter));

    bool is_valid = true;

    //1. Verify AES-GCM Tag
    if (decryptor_) {
        vector<uint8_t> expected_tag(footer.auth_tag_ , footer.auth_tag_ + 16);
        if (!decryptor_->verify_auth_tag(expected_tag)) {
            cout << "\n[SECURITY ALERT] Block " << footer.block_index_ << " AES-GCM Auth Tag mismatch!" << endl;
            is_valid = false;
        }
    }

    //2. Verify CheckSUM
    if (hasher_) {
        string calculated_hash = hasher_->get_sha256_hash();
        if (calculated_hash != footer.checksum_sha256_) {
            cout << "\n[SECURITY ALERT] Block " << footer.block_index_ << " SHA-256 Checksum mismatch!" << endl;
            is_valid = false;
        }
    }

    //--4. Corruption Handling
    if (!is_valid) {
        cout << "[Receiver] Data corruption detected. Rolling back to last safe block : " << footer.block_index_ - 1<< endl;

        //close the file

        uint64_t safe_byte_size = current_block_index_ * BLOCK_SIZE;

        //snip the corrupted chunk
        try {
            fs::resize_file(current_filepath_ , safe_byte_size);
        } catch (exception& e) {
            cout << "[Fatal] Cannot truncate file : " << current_filepath_ << endl;
            return;
        }

        send_ack(false , current_block_index_);
        if (on_transfer_complete_) on_transfer_complete_();
        return;
    }

    //--Successful block transfer--
    current_block_index_++;

    hasher_.emplace();

    if (metadata_.is_compressed_) {
        decompressor_.emplace();
    }

    if (manifest_.is_encrypted_) {
        vector<uint8_t> master_iv(metadata_.master_crypto_iv_ , metadata_.master_crypto_iv_ + 12);

        auto next_block_iv = file_helper::derive_block_iv(master_iv , current_block_index_);
        decryptor_->init_new_block(next_block_iv);
    }
}

void FileReceiver::process_file_eof() {
    outfile_.close();

    //--THE COMMIT--
    try {
        if (fs::exists(final_filepath_)) {
            fs::remove(final_filepath_);
        }

        fs::rename(current_filepath_ , final_filepath_);
        cout << "[Receiver] File transfer complete : " << metadata_.relative_path_ << endl;
    } catch (exception& e) {
        cout << "Failed to finalize .raftpath rename: " << e.what() << endl;
    }

    send_ack(true , 0);

    hasher_.reset();
    decompressor_.reset();

    //---Batch Loop---
    if (manifest_.is_batch_directory_) {
        current_state_ = ReceiverState::AWAITING_METADATA;
        current_file_count_++;
    }else {
        decryptor_.reset();
        cout << "\n[Receiver] All Files in Batch Received successfully!" << endl;
        if (on_transfer_complete_) on_transfer_complete_();
    }
}
