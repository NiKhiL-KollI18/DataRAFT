//
// Created by nikhi on 3/16/2026.
//


#pragma once

#include<string>
#include<cstring>
#include <filesystem>

using namespace std;

struct filemeta {
    uintmax_t file_size_;
    char file_name_[256];
    char extension_[16];


    filemeta(uintmax_t file_size ,const string& file_name , const string& extension) {
        file_size_ = file_size;

        strncpy_s(file_name_ , sizeof(file_name_) , file_name.c_str(), _TRUNCATE);
        file_name_[sizeof(file_name_)-1] = '\0';

        strncpy_s(extension_ , sizeof(extension_) , extension.c_str(), _TRUNCATE);
        extension_[sizeof(extension_)-1] = '\0';
    }
};