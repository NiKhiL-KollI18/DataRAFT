#include <iomanip>
#include <stdexcept>
#include <openssl/evp.h>
#include <sstream>

#include "file_helper.h"


using namespace std;

namespace file_helper {

    StreamDecryptor::StreamDecryptor(const string &password,
        const vector<unsigned char> &salt,
        const vector<unsigned char> &iv) {

        derived_key = derive_key(password , salt);

        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw runtime_error("Error : failed to create OpenSSL cipher context");
        }

        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);

        if (EVP_DecryptInit_ex(cipher_ctx , EVP_aes_256_gcm() , nullptr , nullptr , nullptr) != 1) {
            throw runtime_error("Failed to initialize AES-GCM Decryption");
        }

        if (EVP_CIPHER_CTX_ctrl(cipher_ctx , EVP_CTRL_GCM_SET_IVLEN , static_cast<int>(iv.size()) , nullptr) != 1) {
            throw runtime_error("Failed to set IV length");
        };

        if (EVP_DecryptInit_ex(cipher_ctx , nullptr , nullptr ,  derived_key.data() , iv.data()) != 1) {
            throw runtime_error("Failed to set Key and IV");
        }
    }

    StreamDecryptor::~StreamDecryptor() {
        if (ctx) {
            EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(ctx));
        }
        OPENSSL_cleanse(derived_key.data() , derived_key.size());
    }

    void StreamDecryptor::decrypt_chunk(std::vector<char>& chunk){
        if (chunk.empty()) return;

        int out_len = 0;
        std::vector<char> plaintext(chunk.size() + 16);
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);

        if (EVP_DecryptUpdate(cipher_ctx,
                              reinterpret_cast<unsigned char*>(plaintext.data()),
                              &out_len,
                              reinterpret_cast<const unsigned char*>(chunk.data()),
                              static_cast<int>(chunk.size())) != 1) {
            throw std::runtime_error("Fatal Error: OpenSSL failed to decrypt chunk.");
                              }

        plaintext.resize(out_len);
        chunk = std::move(plaintext);
    }

    [[nodiscard]] bool StreamDecryptor::verify_auth_tag(const vector<unsigned char> &expected_tag) const{
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX*>(ctx);

        if (EVP_CIPHER_CTX_ctrl(cipher_ctx ,
            EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(expected_tag.size()),
            const_cast<unsigned char*>(expected_tag.data())) != 1) {
            return false;
        }

        int out_len = 0;
        vector<unsigned char> final_buffer(16);

        int return_code = EVP_DecryptFinal_ex(cipher_ctx , final_buffer.data() , &out_len);

        return return_code > 0;
    }

    bool StreamDecryptor::check_password_hash(const string &expected_hash) const {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int length = 0;

        if (EVP_Digest(derived_key.data() , derived_key.size() , hash , &length , EVP_sha256() , nullptr) != 1) {
            return false;
        }

        stringstream ss;
        for (unsigned int i = 0 ; i < length ; i++) {
            ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
        }

        return ss.str() == expected_hash;
    }
}
