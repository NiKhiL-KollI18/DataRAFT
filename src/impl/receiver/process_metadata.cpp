#include <array>
#include <filesystem>

#include "receiver.h"
#include "globals.h"
#include "sender.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;
using ui = UIManager;

void FileReceiver::process_metadata(const binary &data) {
    if (data.size() != sizeof(FileMeta)) {
        raft_globals::shutdown(Level::ERR , "Received invalid metadata size. Aborting transfer...");
        send_ack(false, 0);
        return;
    }

    memcpy(&metadata_ , data.data() , sizeof(FileMeta));

    // Calculate the target paths
    string raw_target_path = base_download_path_ + "/" + string(metadata_.relative_path_);
    string raw_raft_path = raw_target_path + ".raftpath";

    //Safely create parent-dir from the raw path instead.
    std::error_code ec;
    fs::path raw_parent_dir = fs::path(raw_raft_path).parent_path();

    if (!raw_parent_dir.empty() && !fs::exists(raw_parent_dir, ec)) {
        fs::create_directories(raw_parent_dir, ec);

        // Double-check existence in case of weird OS drive-root permissions
        if (ec && !fs::exists(raw_parent_dir, ec)) {
            raft_globals::shutdown(Level::ERR , "Could not create directories: " + ec.message() + " : " + raw_parent_dir.string());
            send_ack(false, 0);
            return;
        }
    }

    // CONVERT TO WINDOWS LONG PATHS FOR FILE I/O
    // so std::ofstream can bypass the 260-character limit!
    final_filepath_ = file_helper::to_windows_long_path(raw_target_path);
    current_filepath_ = file_helper::to_windows_long_path(raw_raft_path);

    //---1 . Check for complete files---
    if (fs::exists(final_filepath_)) {
        if (skip_existing_files_) {
            ui::log_internals("[Receiver] File Already exists. Skipping : " + string(metadata_.relative_path_));
            global_bytes_transferred_ += fs::file_size(final_filepath_);

            if (manifest_.is_batch_directory_ && current_file_count_ < manifest_.total_file_count_) {
                current_file_count_++;
            } else {
                // We skipped the very last file in the batch!
                if (manifest_.is_batch_directory_) {
                    ui::print(Level::SUCCESS , "Batch transfer complete! All files received successfully.");
                }else {
                    ui::print(Level::SUCCESS , "File transfer completed.");
                }
                raft_globals::shutdown(Level::SYSTEM , "");
            }

            //send a special flag to indicate skip
            send_ack(true, UINT64_MAX);
            return;
        }
        else {
            ui::log_internals("[Receiver] Replace Enable. Overwriting completed files...");
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

            ui::log_internals("[Receiver] Found partial file. Resuming from block " + to_string(resume_block));

        } else {
            //replace mode : delete partial file
            fs::remove(current_filepath_);
        }
    } else {
        ui::log_internals("[Receiver] Starting new file download : " + string(metadata_.relative_path_) + "");
    }

    //--3. Open file--
    // Defensive check: ensure the stream is completely free before opening a new file!
    if (outfile_.is_open()) {
        outfile_.close();
    }

    //if resuming open in appending mode, else truncate
    outfile_.open(current_filepath_ , ios::binary | (resume_block > 0 ? ios::app : ios::trunc));
    if (!outfile_.is_open()) {
        raft_globals::shutdown(Level::ERR , "Cannot open file for writing : " + current_filepath_);
        send_ack(false, 0);
        return;
    }

    //sync state machine
    current_block_index_ = resume_block;
    bytes_processed_count_ = resume_block * BLOCK_SIZE;
    global_bytes_transferred_ += (resume_block * BLOCK_SIZE);

    //--4. REFORMERS--
    if (!hasher_) hasher_.emplace();
    hasher_->reset();

    if (metadata_.is_compressed_) {
        if (!decompressor_) decompressor_.emplace();
        decompressor_->reset();
    }

    if (manifest_.is_encrypted_) {
        vector<uint8_t> master_iv(metadata_.master_crypto_iv_ , metadata_.master_crypto_iv_ + 12);
        auto block_iv = file_helper::derive_block_iv(master_iv , current_block_index_);

        decryptor_->init_new_block(block_iv);
    }

    total_bytes_received_ = resume_block * BLOCK_SIZE;

    //initializing data receiver
    send_ack(true , resume_block);
    current_state_ = ReceiverState::RECEIVING_DATA;
}