#pragma once

#include <cstdint>

struct FileMeta {
    //file info
    uint64_t file_size_;
    char relative_path_[512];
    char extension_[16];

    //file integrity
    char checksum_sha256_[65];

    //feature flags
    bool is_compressed_;
    bool is_transfer_complete_;

    //UX & OS specific
    uint32_t file_permissions_;
};

struct TransferAck {
    bool accept_transfer_;
    uint64_t resume_from_byte_;
};

struct DataManifest {
    bool is_batch_directory_;

    char folder_name_[256];
    uint64_t total_folder_size_;
    uint32_t total_file_count_;

    char sender_name_[64];
    bool is_encrypted_;
    char password_hash_sha256_[65];
};
