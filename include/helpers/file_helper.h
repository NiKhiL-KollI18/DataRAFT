#pragma once

#include <functional>
#include <string>
#include<vector>

#include "protocol.h"

namespace file_helper {

        struct CryptoParams {
                std::vector<unsigned char> iv;
                std::vector<unsigned char> salt;
        };

        void extract_metadata(const std::string &filepath , FileMeta& metadata , const std::string &sha256hash = "" , bool is_transfer_completed = false);

        void extract_transfer_ack(const std::string &filepath , TransferAck& response , bool accept_offer = true);

        void create_data_manifest(DataManifest& data_manifest , const std::string &filepath , bool is_encrypted ,
                const std::string &password_hash ,
                const std::vector<unsigned char>& salt ,
                const std::vector<unsigned char>& iv);

        std::string calculate_sha256(const std::string &filepath , const std::function<void(size_t , size_t)> &progress_callback);

        bool is_compressible(const std::string &extension);

        std::string sanitize_filename(const std::string &filepath);

        std::string format_file_size(uint64_t bytes);

        std::vector<unsigned char> derive_key(const std::string& password , const std::vector<unsigned char>& salt);

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

                void encrypt_chunk(std::vector<char>& chunk);

                [[nodiscard]] std::vector<unsigned char> get_auth_tag() const;

                [[nodiscard]] CryptoParams get_crypto_params() const;

                [[nodiscard]] std::string get_password_hash() const;

        private:
                void* ctx;
                std::vector<unsigned char> derived_key;
                CryptoParams params;

                static std::vector<unsigned char> generate_random_numbers(size_t length);
        };

        class StreamDecryptor {
        public:
                StreamDecryptor(const std::string& password ,
                        const std::vector<unsigned char>& salt ,
                        const std::vector<unsigned char>& iv
                        );

                ~StreamDecryptor();

                void decrypt_chunk(std::vector<char>& chunk);

                [[nodiscard]] bool verify_auth_tag(const std::vector<unsigned char>& expected_tag) const;

                [[nodiscard]] bool check_password_hash(const std::string &expected_hash) const;
        private:
                void* ctx;
                std::vector<unsigned char> derived_key;
        };
}
