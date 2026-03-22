// // #define _AMD64_
// #define WIN32_LEAN_AND_MEAN
// #include <windows.h>

#include "file_helper.h"

#include<iostream>
#include <format>
#include <openssl/evp.h>
#include <filesystem>
#include <unordered_set>

constexpr size_t SHA256_BUFFER_SIZE = 64 * 1024 * 1024;

using namespace std;

namespace file_helper {

    auto progress_callback_ = [](size_t processed , size_t total) {

        string processed_size = format_file_size(processed);
        string total_size = format_file_size(total);

        cout << "\r[processed : ]" << processed_size << " / " << total_size << flush;
    };

    void extract_metadata(const string &filepath, FileMeta &metadata , const string& sha256hash , bool is_transfer_complete) {
        namespace fs = std::filesystem;
        fs::path file_path(filepath);

        memset(&metadata , 0 , sizeof(FileMeta));

        //file info
        metadata.file_size_ = fs::file_size(file_path);

        string relative_path = sanitize_filename(filepath);
        strncpy(metadata.relative_path_ , relative_path.c_str() ,
            sizeof(metadata.relative_path_) - 1);

        string extension = file_path.extension().string();
        strncpy(metadata.extension_, extension.c_str(), sizeof(metadata.extension_) - 1);

        //file integrity
        string checksum = sha256hash;
        strncpy(metadata.checksum_sha256_ , checksum.c_str() , sizeof(metadata.checksum_sha256_) - 1);

        metadata.is_compressed_ = is_compressible(filepath);

        //feature flags
        metadata.is_transfer_complete_ = is_transfer_complete;

        metadata.file_permissions_ = static_cast<uint64_t>(fs::status(filepath).permissions());
    }

    // string calculate_sha256(const string &filepath , const function<void(size_t , size_t)> &progress_callback) {
    //     HANDLE h_file = CreateFile(filepath.c_str() ,GENERIC_READ ,
    //         FILE_SHARE_READ ,nullptr , OPEN_EXISTING ,
    //         FILE_ATTRIBUTE_NORMAL , nullptr);
    //     if (h_file == INVALID_HANDLE_VALUE) return "Error : could not open the file.";
    //
    //     LARGE_INTEGER file_size;
    //     GetFileSizeEx(h_file , &file_size);
    //     size_t total_bytes = file_size.QuadPart;
    //
    //     HANDLE h_map = CreateFileMappingA(h_file , nullptr ,
    //         PAGE_READONLY , 0 , 0 , nullptr);
    //
    //     if (h_map == nullptr) {
    //         CloseHandle(h_file);
    //         return "Error : could not create file mapping.";
    //     }
    //
    //     EVP_MD_CTX* context = EVP_MD_CTX_new();
    //     EVP_DigestInit_ex(context , EVP_sha256() , nullptr);
    //
    //     size_t bytes_remaining = total_bytes;
    //     unsigned long long last_callback_mark = -1;
    //
    //     unsigned long long offset = 0;
    //
    //     while (bytes_remaining > 0) {
    //         size_t bytes_to_map = min(SHA256_BUFFER_SIZE , bytes_remaining);
    //
    //         DWORD offset_high = (offset >> 32) & 0xFFFFFFFF;
    //         DWORD offset_low = offset & 0xFFFFFFFF;
    //
    //         const char* mapped_data = static_cast<const char*>(MapViewOfFile(h_map , FILE_MAP_READ ,
    //             offset_high , offset_low , bytes_to_map));
    //
    //         if (mapped_data == nullptr) {
    //             EVP_MD_CTX_free(context);
    //             CloseHandle(h_map);
    //             CloseHandle(h_file);
    //             return "Error : error mapping at offset " + to_string(offset);
    //         }
    //         EVP_DigestUpdate(context , mapped_data , bytes_to_map);
    //         UnmapViewOfFile(mapped_data);
    //
    //         offset += bytes_to_map;
    //         bytes_remaining -= bytes_to_map;
    //
    //         unsigned long long current_gb = offset / (1024 * 1024 * 1024);
    //         if (current_gb != last_callback_mark) {
    //             if (progress_callback) {
    //                 progress_callback(offset , total_bytes); //report progress
    //             }
    //             last_callback_mark = current_gb;
    //         }
    //     }
    //
    //     if (progress_callback) progress_callback(total_bytes, total_bytes);
    //
    //     unsigned char hash_bytes[EVP_MAX_MD_SIZE];
    //     unsigned int hash_length = 0;
    //     EVP_DigestFinal_ex(context , hash_bytes, &hash_length);
    //
    //     EVP_MD_CTX_free(context);
    //     CloseHandle(h_file);
    //     CloseHandle(h_map);
    //
    //     stringstream ss;
    //     for (int i = 0; i < hash_length; i++) {
    //         ss << hex << setw(2) << setfill('0') << static_cast<int>(hash_bytes[i]);
    //     }
    //
    //     return ss.str();
    // }

    string sanitize_filename(const string &filepath) {
        namespace fs = std::filesystem;

        fs::path input_path(filepath);
        fs::path safe_path;

        const string illegal_chars = "<>:\"|?*";

        for (const auto& component : input_path) {
            string part = component.string();

            if (part == "." || part == "/" || part == "\\" || part.find(':') != string::npos) {
                continue;
            }

            part.erase(remove_if(part.begin() , part.end() , [&illegal_chars](char c) {
                return illegal_chars.find(c) != string::npos || c < 32;
            }) , part.end());

            if (!part.empty()) {
                safe_path /= part;
            }
        }
        return safe_path.generic_string();
    }

    string format_file_size(uint64_t bytes) {
        auto size = static_cast<double>(bytes);
        int unit_idx = 0;

        const vector<string> units = {"B" , "KiB" , "MiB" , "GiB" , "TiB"};

        while (size >= 1024 || unit_idx == 4) {
            size /= 1024;
            unit_idx++;
        }
        return format("{:.2f} {}" , size , units[unit_idx]);
    }

    vector<unsigned char>derive_key(string& password , const vector<unsigned char>& salt) {
        vector<unsigned char> key(32);

        constexpr int iterations = 100000;

        int success = PKCS5_PBKDF2_HMAC(
            password.c_str() ,
            static_cast<int>(password.length()) ,
            salt.data() ,
            static_cast<int>(salt.size()),
            iterations ,
            EVP_sha256() ,
            static_cast<int>(key.size()),
            key.data()
            );

        if (success != 1) {
            throw::std::runtime_error("Fatal Error : PBKDF2 key derivation failed");
        }

        return key;
    }

    bool is_compressible(const string &extension) {

        if (extension.empty()) return true;

        static const unordered_set<string> incompressible_ext = {
            // Archives & Installers
            ".zip", ".rar", ".7z", ".tar", ".gz", ".exe", ".iso", ".msi", ".apk",
            // Video
            ".mp4", ".mkv", ".avi", ".mov", ".webm", ".wmv", ".flv",
            // Audio
            ".mp3", ".aac", ".ogg", ".flac", ".m4a", ".wav", // wav is uncompressed, but huge. Compression varies.
            // Images
            ".jpg", ".jpeg", ".png", ".gif", ".webp", ".heic", ".bmp",
            // Documents that are already compressed/zipped
            ".pdf", ".docx", ".xlsx", ".pptx", ".epub"
        };

        return !(incompressible_ext.contains(extension));
    }

    void create_data_manifest(DataManifest &data_manifest,
        const string &filepath, bool is_encrypted, const string &password_hash
        , const vector<unsigned char> &salt, const vector<unsigned char> &iv) {

        namespace fs = filesystem;

        memset(&data_manifest, 0, sizeof(DataManifest));

        fs::path p(filepath);

        if (!fs::exists(p)) throw runtime_error("Error : Path does not exist -> " + filepath);

        string safe_name = sanitize_filename(p.filename().string());
        strncpy(data_manifest.folder_name_, safe_name.c_str(), sizeof(data_manifest.folder_name_) - 1);

        if (fs::is_directory(p)) {
            data_manifest.is_batch_directory_ = true;
            uint64_t total_size = 0;
            uint32_t file_count = 0;

            for (const auto& entry : fs::recursive_directory_iterator(p)) {
                total_size += fs::file_size(entry);
                file_count++;
            }
        }
        else {
            //single file
            data_manifest.is_batch_directory_ = false;
            data_manifest.total_file_count_ = 1;
            data_manifest.total_folder_size_ = fs::file_size(p);
        }

        //encryption
        data_manifest.is_encrypted_ = is_encrypted;
        if (is_encrypted) {
            strncpy(data_manifest.password_hash_sha256_, password_hash.c_str(), sizeof(data_manifest.password_hash_sha256_) - 1);
            memcpy(data_manifest.crypto_salt_, salt.data(), min(sizeof(data_manifest.crypto_salt_), salt.size()));
            memcpy(data_manifest.crypto_iv_, iv.data(), min(sizeof(data_manifest.crypto_iv_), iv.size()));
        }
    }

    void extract_transfer_ack(const string &filepath, TransferAck &response, bool accept_offer) {
        namespace fs = filesystem;

        std::memset(&response, 0, sizeof(TransferAck));
        response.accept_transfer_ = accept_offer;

        if (accept_offer && fs::exists(filepath)) {
            response.resume_from_byte_ = fs::file_size(filepath);
        }else {
            response.resume_from_byte_ = 0;
        }
    }
}
