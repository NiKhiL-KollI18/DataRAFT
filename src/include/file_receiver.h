#pragma once
#include <fstream>
#include <string>

#include "protocol.h"
#include "rtc/rtc.hpp"

class FileReceiver {
private:
    std::string base_download_path_;
    std::ofstream outfile_;

    FileMeta metadata_;

    std::string decryption_key_;

    uint64_t bytes_received_so_far_;
    uint64_t current_file_expected_size_;

public:
    FileReceiver(const std::string &download_path);

    ~FileReceiver();

    void handle_data_manifest(const DataManifest& manifest , rtc::DataChannel &data_channel);

    void handle_file_meta(const FileMeta& metadata , rtc::DataChannel &data_channel);

    void receive_chunk(const std::vector<char>& chunk);

    void finalize_transfer();
};
