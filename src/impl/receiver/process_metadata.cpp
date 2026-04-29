#include <filesystem>

#include "receiver.h"
#include "globals.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;

void FileReceiver::process_metadata(const binary &data) {
    if (data.size() != sizeof(FileMeta)) {
        raft_globals::shutdown("Received invalid metadata size. Aborting transfer...");
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
        raft_globals::shutdown(std::string("Could not create directories: ") + e.what());
        send_ack(false, 0);
        return;
    }

    //---1 . Check for complete files---
    if (fs::exists(final_filepath_)) {
        if (skip_existing_files_) {
            cout << "[Receiver] File Already exists. Skipping : " << metadata_.relative_path_ << endl;

            if (manifest_.is_batch_directory_ && current_file_count_ < manifest_.total_file_count_) {
                current_file_count_++;
            } else {
                // We skipped the very last file in the batch!
                raft_globals::shutdown("All files in batch already exist. Transfer complete!");
            }

            //send special flag to indicate skip
            send_ack(true, UINT64_MAX);
            return;
        }
        else {
            cout << "[Receiver] Replace Enabled. Overwriting completed files..." << endl;
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

            cout << "[Receiver] Found partial file. Resuming from block " << resume_block << endl;
        } else {
            //replace mode : delete partial file
            fs::remove(current_filepath_);
        }
    } else {
        cout << "[Receiver] Starting new file download : " << metadata_.relative_path_ << endl;
    }

    //--3. Open file--
    //if resuming open in append mode, else truncate
    outfile_.open(current_filepath_ , ios::binary | (resume_block > 0 ? ios::app : ios::trunc));
    if (!outfile_.is_open()) {
        raft_globals::shutdown("Cannot open file for writing : " + current_filepath_);
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