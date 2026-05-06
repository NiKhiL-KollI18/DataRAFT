#include "sender.h"
#include "globals.h"
#include "config_manager.h"
#include "ui_manager.h"

#include <nlohmann/json_fwd.hpp>
#include <utility>

using namespace rtc;
using namespace std;

using json = nlohmann::json;
using ui = UIManager;

Sender::Sender(const queue<string> &files, string base_dir,
               const shared_ptr<DataChannel> &data_channel,
               bool is_encrypted, const string &password)
        : base_directory_(std::move(base_dir)), pending_files_(files), data_channel_(data_channel) {
    total_files_in_batch_ = pending_files_.size();

    MAX_QUEUE_SIZE = ConfigManager::get_buffer_limit();

    current_filepath_ = pending_files_.front();
    pending_files_.pop();

    string safe_read_path = file_helper::to_windows_long_path(current_filepath_);

    infile_.open(safe_read_path , ios::binary);
    if (!infile_.is_open()) {
        throw runtime_error("Fatal Error! Cannot open the file ->" + safe_read_path);
    }

    vector<uint8_t> salt;
    vector<uint8_t> master_iv;
    string pass_hash;

    file_helper::extract_metadata(current_filepath_ , base_directory_ , metadata_);

    if (is_encrypted) {
        encryptor_.emplace(password);
        password_ = password;

        salt = encryptor_->get_salt();
        pass_hash = encryptor_->get_password_hash();

        master_iv = encryptor_->generate_new_master_iv();
        memcpy(metadata_.master_crypto_iv_ , master_iv.data() , 12);
    }
    memset(&data_manifest_, 0, sizeof(DataManifest));
    file_helper::create_data_manifest(data_manifest_ , base_directory_ , is_encrypted, pass_hash , salt);

    last_speed_calc_time_ = std::chrono::steady_clock::now();
    bytes_sent_since_last_calc_ = 0;
    current_speed_bps_ = 0.0;

    //data_manifest_.total_file_count_ = total_files_in_batch_;
    //data_manifest_.is_batch_directory_ = (total_files_in_batch_ > 1);
}

Sender::~Sender() {
    is_file_reading_completed_ = true;
    queue_cv_.notify_all();

    if (producer_thread_.joinable()) {
        producer_thread_.join();
    }
    if (infile_.is_open()) {
        infile_.close();
    }
}


