#include <iomanip>
#include <openssl/evp.h>
#include <stdexcept>
#include <sstream>

#include "file_helper.h"

using namespace std;

namespace file_helper{
    StreamingHasher::StreamingHasher() : is_finalised_(false) {
        ctx = EVP_MD_CTX_new();

        if (!ctx) {
            throw::runtime_error("Error : Failed to create openSSL digest context");
        }

        if (EVP_DigestInit_ex(static_cast<EVP_MD_CTX*>(ctx) , EVP_sha256() , nullptr ) != 1) {
            EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx));
            throw::runtime_error("Error : Failed to initialize sha256 digestion");
        }
    }

    StreamingHasher::~StreamingHasher() {
        if (ctx) {
            EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx));
        }
    }

    void StreamingHasher::update_hash(const std::vector<char> &chunk) {
        if (chunk.empty() || is_finalised_) return;

        if (EVP_DigestUpdate(static_cast<EVP_MD_CTX*>(ctx) , chunk.data() , chunk.size() ) != 1) {
            throw::runtime_error("Error : Failed to update SHA256 hash");
        }
    }

    std::string StreamingHasher::get_sha256_hash() {
        if (is_finalised_) {
            throw::runtime_error("Error : Hash is already finalized");
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int length = 0;

        if (EVP_DigestFinal_ex(static_cast<EVP_MD_CTX*>(ctx) , hash, &length) != 1) {
            throw::runtime_error("Error : Failed to finalize SHA-256 hash.");
        }

        is_finalised_ = true;

        stringstream ss;
        for (int i = 0 ; i < length ; i++) {
            ss << hex << setw(2) << setfill('0') << static_cast<int> (hash[i]);
        }

        return ss.str();
    }

    void StreamingHasher::reset() {
        // EVP_DigestInit_ex automatically resets an existing context cleanly
        if (EVP_DigestInit_ex(static_cast<EVP_MD_CTX*>(ctx) , EVP_sha256() , nullptr ) != 1) {
            throw runtime_error("Error : Failed to reset SHA256 context");
        }
        is_finalised_ = false;
    }
}
