#include <array>
#include <iomanip>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <openssl/err.h>

#include "file_helper.h"


using namespace std;

namespace file_helper {
    StreamEncryptor::StreamEncryptor(const string &password) {
        salt_ = generate_random_bytes(16);
        derived_key_ = derive_key(password , salt_);

        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw runtime_error("Error : failed to create OpenSSL cipher context");
        }

        //preload the AES-GCM cipher type
        EVP_EncryptInit_ex(static_cast<EVP_CIPHER_CTX*>(ctx) , EVP_aes_256_gcm() , nullptr , nullptr , nullptr);
        //preload the IVLEN once
        EVP_CIPHER_CTX_ctrl(static_cast<EVP_CIPHER_CTX*>(ctx) , EVP_CTRL_GCM_SET_IVLEN , 12 , nullptr);
    }

    vector<uint8_t> StreamEncryptor::generate_random_bytes(size_t length) {
        vector<uint8_t> buffer(length);

        if (RAND_bytes(buffer.data() , length) != 1) {
            throw runtime_error("Error : failed to generate random bytes");
        }
        return buffer;
    }

    StreamEncryptor::~StreamEncryptor() {
        if (ctx) {
            EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(ctx)) ;
        }

        OPENSSL_cleanse(derived_key_.data() , derived_key_.size());
    }

    vector<uint8_t> StreamEncryptor::generate_new_master_iv() {

        return generate_random_bytes(12);
    }

    void StreamEncryptor::init_new_block(const array<uint8_t , 12> &block_iv) {
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);

        if (EVP_EncryptInit_ex(cipher_ctx , nullptr , nullptr , derived_key_.data() , block_iv.data()) != 1) {
            char err_msg[256];
            ERR_error_string_n(ERR_get_error(), err_msg, sizeof(err_msg));
            throw runtime_error("Failed to swap IV" + string(err_msg));
        }
    }

    void StreamEncryptor::encrypt_chunk(vector<char> &chunk) {
        if (chunk.empty()) return;

        //Zero copy encryption
        int out_len = 0;
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);

        if (EVP_EncryptUpdate(cipher_ctx ,
            reinterpret_cast<unsigned char*>(chunk.data()) ,
            &out_len ,
            reinterpret_cast<const unsigned char*>(chunk.data()) ,
            static_cast<int>(chunk.size())) != 1)
        {
            throw runtime_error("Fatal Error: OpenSSL failed to encrypt chunk.");
        }
    }

    vector<uint8_t> StreamEncryptor::get_auth_tag() const {
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);
        int out_len = 0;

        vector<unsigned char> final_buffer(16);
        if (EVP_EncryptFinal_ex(cipher_ctx , final_buffer.data() , &out_len) != 1) {
            throw runtime_error("Fatal Error: Failed to finalize AES-GCM Encryption.");
        }

        vector<unsigned char> tag(16);
        if (EVP_CIPHER_CTX_ctrl(cipher_ctx , EVP_CTRL_GCM_GET_TAG , static_cast<int>(tag.size()) , tag.data()) != 1) {
            throw runtime_error("Error : Failed to get auth tag");
        }

        return tag;
    }

    std::string StreamEncryptor::get_password_hash() const {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int length = 0;

        if (EVP_Digest(derived_key_.data() , static_cast<int>(derived_key_.size()) ,
            hash , &length , EVP_sha256() , nullptr) != 1) {
            throw runtime_error("Error : Failed to generate password hash");
        }

        stringstream ss;
        for (unsigned int i = 0 ; i < length ; i++) {
            ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    vector<uint8_t> StreamEncryptor::get_salt() const {
        return salt_;
    }
}
