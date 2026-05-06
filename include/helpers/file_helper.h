#pragma once

#include <cstdint> // Required for uint8_t
#include <functional>
#include <queue>
#include <string>
#include <vector>

#include "protocol.h"

namespace file_helper {

        std::string to_windows_long_path(const std::string &standard_filepath);

        void extract_metadata(const std::string &filepath , const std::string &base_target_path , FileMeta& metadata);

        void extract_transfer_ack(const std::string &filepath , TransferAck& response , bool accept_offer = true);

        void create_data_manifest(DataManifest& data_manifest , const std::string &filepath , bool is_encrypted ,
                const std::string &password_hash = "",
                const std::vector<uint8_t>& salt  = {});

        std::string calculate_sha256(const std::string &filepath , const std::function<void(size_t , size_t)> &progress_callback);

        bool is_compressible(const std::string &extension);

        std::string sanitize_filename(const std::string &filepath);

        std::string format_file_size(uint64_t bytes);

        std::vector<uint8_t> derive_key(const std::string& password , const std::vector<uint8_t>& salt);

        std::array<uint8_t , 12> derive_block_iv(const std::vector<uint8_t> &master_iv , uint64_t block_index);

        void build_transfer_queue(const std::string& target_path, std::queue<std::string>& pending_files, std::string& base_directory);

        class StreamingHasher {
        private:
                void* ctx;
                bool is_finalised_;
        public:
                StreamingHasher();
                ~StreamingHasher();

                void update_hash(const std::vector<char>& chunk);

                std::string get_sha256_hash();
        };

        class StreamCompressor {
        public:
                StreamCompressor(int compression_level = 3);
                ~StreamCompressor();
                void compress_chunk(std::vector<char>&chunk , bool is_last_chunk);
        private:
                void* ctx;
                std::vector<char> internal_buffer_;
        };

        class StreamDecompressor {
        public:
                StreamDecompressor();
                ~StreamDecompressor();
                void decompress_chunk(std::vector<char>& chunk);
        private:
                void* ctx;
                std::vector<char> internal_buffer_;
        };


        class StreamEncryptor {
        public:
                StreamEncryptor(const std::string& password);
                ~StreamEncryptor();

                [[nodiscard]]std::vector<uint8_t> generate_new_master_iv();

                void init_new_block(const std::array<uint8_t , 12>& block_iv);

                void encrypt_chunk(std::vector<char>& chunk);

                [[nodiscard]] std::vector<uint8_t> get_auth_tag() const;

                [[nodiscard]] std::vector<uint8_t> get_salt() const;

                [[nodiscard]] std::string get_password_hash() const;

        private:
                void* ctx;
                std::vector<uint8_t> derived_key_;
                std::vector<uint8_t> salt_;

                static std::vector<uint8_t> generate_random_bytes(size_t length);
        };

        class StreamDecryptor {
        public:
                StreamDecryptor(const std::string& password ,
                        const std::vector<uint8_t>& salt);

                ~StreamDecryptor();

                void init_new_block(const std::array<uint8_t , 12>& block_iv);

                void decrypt_chunk(std::vector<char>& chunk);

                [[nodiscard]] bool verify_auth_tag(const std::vector<uint8_t>& expected_tag) const;
                [[nodiscard]] bool check_password_hash(const std::string &expected_hash) const;
        private:
                void* ctx;
                std::vector<uint8_t> derived_key_;
        };
}