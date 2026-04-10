#include <iomanip>
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>

#include "file_helper.h"


using namespace std;

namespace file_helper {
    StreamEncryptor::StreamEncryptor(const string &password) {
        params.salt = generate_random_numbers(16);
        params.iv = generate_random_numbers(12);

        derived_key = derive_key(password , params.salt);

        ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create OpenSSL cipher context.");
        }

        if (EVP_EncryptInit_ex(static_cast<EVP_CIPHER_CTX *>(ctx) , EVP_aes_256_gcm() , nullptr , nullptr , nullptr) != 1) {
            throw std::runtime_error("Failed to initialize AES-GCM cipher");
        }
        if (EVP_CIPHER_CTX_ctrl(static_cast<EVP_CIPHER_CTX*>(ctx) , EVP_CTRL_GCM_SET_IVLEN ,
            static_cast<int>(params.iv.size()) , nullptr) != 1) {
            throw std::runtime_error("Failed to initialize IV length");
            }

        if (EVP_EncryptInit_ex(static_cast<EVP_CIPHER_CTX *>(ctx) ,
            nullptr , nullptr , derived_key.data(), params.iv.data()) != 1) {
            throw std::runtime_error("Failed to set Key and IV");
            }
    }

    StreamEncryptor::~StreamEncryptor() {
        if (ctx) {
            EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX *>(ctx)) ;
        }

        OPENSSL_cleanse(derived_key.data() , derived_key.size());
    }

    vector<unsigned char> StreamEncryptor::generate_random_numbers(size_t length) {
        vector<unsigned char> buffer(length);

        if (RAND_bytes(buffer.data() , static_cast<int>(length)) != 1) {
            throw std::runtime_error("Error : OpenSSL failed to generate secure bytes.");
        }

        return buffer;
    }

    CryptoParams StreamEncryptor::get_crypto_params() const {
        return params;
    }

    void StreamEncryptor::encrypt_chunk(vector<char> &chunk){
        if (chunk.empty()) return;

        int out_len = 0;

        std::vector<char> ciphertext(chunk.size() + 16);

        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX *>(ctx);

        if (EVP_EncryptUpdate(cipher_ctx ,
            reinterpret_cast<unsigned char*>(ciphertext.data()) ,
            &out_len ,
            reinterpret_cast<unsigned char*>(chunk.data()) ,
            static_cast<int>(chunk.size())) != 1) {
            throw runtime_error("Error : Failed to encrypt chunk.");
        }

        ciphertext.resize(out_len);
        chunk = std::move(ciphertext);
    }

    [[nodiscard]] vector<unsigned char> StreamEncryptor::get_auth_tag() const{
        auto* cipher_ctx = static_cast<EVP_CIPHER_CTX *>(ctx);
        int out_len = 0;

        vector<unsigned char> final_buffer(16);
        if (EVP_EncryptFinal_ex(cipher_ctx ,
            final_buffer.data() ,
            &out_len) != 1) {
            throw runtime_error("Error : Failed to finalize AES-GCM encryption.");
        }

        vector<unsigned char> tag(16);
        if (EVP_CIPHER_CTX_ctrl(cipher_ctx , EVP_CTRL_GCM_GET_TAG , static_cast<int>(tag.size()) , tag.data()) != 1) {
            throw runtime_error("Error : Failed to get AES-GCM auth tag");
        }

        return tag;
    }

    std::string StreamEncryptor::get_password_hash() const {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int length = 0;

        if (EVP_Digest(derived_key.data() , static_cast<int>(derived_key.size()) , hash ,
            &length , EVP_sha256() , nullptr) != 1) {
            throw runtime_error("Error : Failed to generate password hash");
        }

        stringstream ss;
        for (unsigned int i = 0 ; i < length ; i++) {
            ss << hex << setw(2) << setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
}
