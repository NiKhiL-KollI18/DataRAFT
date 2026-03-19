#pragma once

#include <fstream>

#include "protocol.h"
#include "helpers/file_helper.h"
#include "rtc/peerconnection.hpp"

constexpr int MAX_BUFFER_SIZE = 128 * 1024;
constexpr int BUCKET_SIZE = 32 * 1024;

class FileTransfer {
private:
    std::string filepath_;
    FileMeta metadata{};

    std::ifstream infile_;

    void send_metadata(FileMeta &metadata);

public:
    FileTransfer(const std::string &filepath) {
        filepath_ = filepath;
        file_helper::extract_metadata(filepath_ , metadata);
    }

    ~FileTransfer();

    void send_metadata(rtc::DataChannel &data_channel);

    void send_file_chunks(rtc::DataChannel &data_channel);
};