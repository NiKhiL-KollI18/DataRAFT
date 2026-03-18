#pragma once

#include <string>
#include<vector>

#include "include/protocol.h"

void extract_metadata(const std::string &filepath , FileMeta& metadata);

void compress_chunk(std::vector<char>& chunk);

std::string caliculate_sha256(std::string &filepath);

std::string sanitize_filename(const std::string &filepath);

std::string format_file_size(uint64_t bytes);
//open to ideas