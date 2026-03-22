#pragma once

#include <fstream>
#include <string>

#include "protocol.h"
#include "file_helper.h"
#include "rtc/peerconnection.hpp"

constexpr int MAX_BUFFER_SIZE = 128 * 1024;
constexpr int BUCKET_SIZE = 32 * 1024;

class FileSender {
private:
    std::string filepath_;
    FileMeta metadata{};

    std::ifstream infile_;

    void send_metadata(FileMeta &metadata);

public:
    FileSender(const std::string &filepath) {
        filepath_ = filepath;
        file_helper::extract_metadata(filepath_ , metadata);
    }

    ~FileSender();

    void send_metadata(rtc::DataChannel &data_channel);

    void send_file_chunks(rtc::DataChannel &data_channel);
};