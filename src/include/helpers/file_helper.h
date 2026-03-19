#pragma once

#include <string>
#include<vector>

#include "include/protocol.h"

namespace file_helper {
    void extract_metadata(const std::string &filepath , FileMeta& metadata);

    void compress_chunk(std::vector<char>& chunk);

    std::string calculate_sha256(const std::string &filepath);

    std::string sanitize_filename(const std::string &filepath);

    std::string format_file_size(uint64_t bytes);

    void encrypt_chunk(std::vector<char>& chunk , const std::string &encryption_key);

    void decrypt_chunk(std::vector<char>& chunk , const std::string &decryption_key);
    //open to ideas

}