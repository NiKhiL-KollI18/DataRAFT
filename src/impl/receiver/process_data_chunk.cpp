#include <filesystem>

#include "receiver.h"
#include "globals.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;
using ui = UIManager;


void FileReceiver::process_data_chunks(vector<char> &&chunk) {
    if (chunk.empty()) return;

    try {
        // 1. Decrypt
        if (decryptor_) decryptor_->decrypt_chunk(chunk);

        // 2.Decompress
        if (decompressor_) decompressor_->decompress_chunk(chunk);

        // 3.hash
        if (hasher_) hasher_->update_hash(chunk);

        // 4. Write to disk
        outfile_.write(chunk.data(), chunk.size());
        bytes_processed_count_ += chunk.size();

        //Progress & speed math
        size_t chunk_size = chunk.size();
        total_bytes_received_ += chunk_size;
        global_bytes_transferred_ += chunk_size;
        bytes_received_since_last_calc_ += chunk_size;

        auto now = std::chrono::steady_clock::now();
        auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_calc_time_).count();

        if (time_diff_ms >= 500) {
            current_speed_bps_ = static_cast<double>(bytes_received_since_last_calc_) / (time_diff_ms / 1000.0);

            bytes_received_since_last_calc_ = 0;
            last_speed_calc_time_ = now;
        }

        uint64_t display_file_num = current_file_count_;

        ui::draw_progress_bar(global_bytes_transferred_ , manifest_.total_folder_size_ ,
            display_file_num , manifest_.total_file_count_ ,
            final_filepath_ , current_speed_bps_);

    } catch (const std::exception& e) {
        raft_globals::shutdown(Level::ERROR , std::string("Pipeline Error during data processing: ") + e.what());
    }
}