#include <filesystem>

#include "receiver.h"
#include "globals.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;


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
        raft_globals::shutdown(std::string("Pipeline Error during data processing: ") + e.what());
    }
}