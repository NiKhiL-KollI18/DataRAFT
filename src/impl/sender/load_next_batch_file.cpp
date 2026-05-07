#include "globals.h"
#include "sender.h"
#include "ui_manager.h"

using namespace std;
using ui = UIManager;

bool Sender::load_next_batch_file() {
    if (pending_files_.empty()) return false;

    current_filepath_ = pending_files_.front();
    pending_files_.pop();

    string safe_read_path = file_helper::to_windows_long_path(current_filepath_);
    infile_.open(safe_read_path, ios::binary);
    if (!infile_.is_open()) {
        raft_globals::shutdown(Level::ERR , "Cannot open next file in batch: " + safe_read_path);
        return false;
    }

    memset(&metadata_, 0, sizeof(FileMeta));
    file_helper::extract_metadata(current_filepath_, base_directory_, metadata_);

    if (data_manifest_.is_encrypted_) {
        auto new_master_iv = encryptor_->generate_new_master_iv();
        memcpy(metadata_.master_crypto_iv_, new_master_iv.data(), 12);
    }
    ui::log_internals("[Sender] Loading next file in batch: " + current_filepath_);

    current_state_ = SenderState::WAITING_META_ACK;
    data_channel_->send(reinterpret_cast<const std::byte*>(&metadata_), sizeof(FileMeta));

    return true;
}