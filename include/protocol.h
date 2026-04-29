#pragma once

#include <cstdint>

#pragma pack(push , 1)

enum class PacketType : uint8_t {
    RAW_DATA = 0x00,
    BLOCK_FOOTER = 0x01,
    FILE_EOF = 0x02
};

struct FileMeta {
    //file info
    uint64_t file_size_;
    char relative_path_[512];
    char extension_[16];

    //file properties
    bool is_compressed_;
    uint32_t file_permissions_;

    //master key for the specific file
    uint8_t master_crypto_iv_[12];
};

struct BlockFooter {
    uint64_t block_index_; //For deriving chunk based IV
    uint8_t auth_tag_[16];
    char checksum_sha256_[65];
};

struct TransferAck {
    bool accept_transfer_;
    uint64_t resume_from_block_;
};

struct DataManifest {
    bool is_batch_directory_;

    char folder_name_[256];
    uint64_t total_folder_size_;
    uint32_t total_file_count_;

    char sender_name_[64];
    bool is_encrypted_;
    char password_hash_sha256_[65];

    uint8_t crypto_salt_[16]; //master key for specific transfer
};

#pragma pack(pop)