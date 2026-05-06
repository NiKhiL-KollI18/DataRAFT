#include <filesystem>

#include "sender.h"
#include "receiver.h"
#include "globals.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;
using ui = UIManager;

void FileReceiver::process_block_footer(vector<char> &&footer_data) {
    if (footer_data.size() != sizeof(BlockFooter)) {
        raft_globals::shutdown(Level::ERROR , "Received corrupted block footer (Network Desync).");
        return;
    }

    BlockFooter footer{};
    memcpy(&footer , footer_data.data() , sizeof(BlockFooter));

    bool is_valid = true;

    //1. Verify AES-GCM Tag
    if (decryptor_) {
        vector<uint8_t> expected_tag(footer.auth_tag_ , footer.auth_tag_ + 16);
        if (!decryptor_->verify_auth_tag(expected_tag)) {
            ui::log_internals("[SECURITY ALERT] Block " + to_string(footer.block_index_) + " AES-GCM Auth Tag mismatch!");
            is_valid = false;
        }
    }

    //2. Verify CheckSUM
    if (hasher_) {
        string calculated_hash = hasher_->get_sha256_hash();
        if (calculated_hash != footer.checksum_sha256_) {
            ui::log_internals("[SECURITY ALERT] Block" + to_string(footer.block_index_) + " SHA-256 Checksum mismatch!");
            is_valid = false;
        }
    }

    //--4. Corruption Handling
    if (!is_valid) {
        ui::log_internals("[Receiver] Data corruption detected. Rolling back to last safe block:" +
            to_string(footer.block_index_ == 0 ? 0 : footer.block_index_ - 1));
        if (outfile_.is_open()) {
            outfile_.close();
        }

        uint64_t safe_byte_size = current_block_index_ * BLOCK_SIZE;

        //snip the corrupted chunk
        try {
            fs::resize_file(current_filepath_ , safe_byte_size);
        } catch (exception& e) {
            raft_globals::shutdown(Level::ERROR , std::string("Cannot truncate corrupted file: ") + e.what());
            return;
        }

        send_ack(false , current_block_index_);
        raft_globals::shutdown(Level::ERROR , "Transfer aborted due to block corruption. Restart transfer to resume safely.");
        return;
    }

    //--Successful block transfer--
    current_block_index_++;

    hasher_.emplace();

    if (metadata_.is_compressed_) {
        decompressor_.emplace();
    }

    if (manifest_.is_encrypted_) {
        vector<uint8_t> master_iv(metadata_.master_crypto_iv_ , metadata_.master_crypto_iv_ + 12);

        auto next_block_iv = file_helper::derive_block_iv(master_iv , current_block_index_);
        decryptor_->init_new_block(next_block_iv);
    }
}
