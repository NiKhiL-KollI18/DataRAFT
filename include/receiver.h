#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstring>

#include "file_helper.h"
#include "protocol.h"
#include "rtc/rtc.hpp"

class FileReceiver {
private:
    enum class ReceiverState {
        AWAITING_MANIFEST,
        AWAITING_PASSWORD,
        AWAITING_METADATA,
        RECEIVING_DATA
    };

    //State & Network
    ReceiverState current_state_ = ReceiverState::AWAITING_MANIFEST;
    std::string base_download_path_;
    std::ofstream outfile_;

    bool skip_existing_files_{true};
    std::string final_filepath_;

    std::string current_filepath_;
    std::string password_; //provided by user via CLI

    //trackers
    uint64_t global_bytes_transferred_ = 0;
    uint64_t total_bytes_received_ = 0;
    uint64_t bytes_received_since_last_calc_ = 0;
    double current_speed_bps_ = 0.0;
    std::chrono::steady_clock::time_point last_speed_calc_time_;

    //Handshake Structs
    DataManifest manifest_{};
    FileMeta metadata_{};

    //Progress Tracking
    uint64_t bytes_processed_count_ = 0;
    uint64_t last_significant_point_size_ = 0;
    uint32_t current_file_count_ = 1; //start from 1

    //Block tracking
    uint64_t current_block_index_ = 0;

    std::shared_ptr<rtc::DataChannel> data_channel_;

    //transformers
    std::optional<file_helper::StreamDecompressor> decompressor_;
    std::optional<file_helper::StreamDecryptor> decryptor_;
    std::optional<file_helper::StreamingHasher> hasher_;

    //internal logic handlers
    void process_manifest(const rtc::binary& data);
    void handle_password_auth();
    void process_metadata(const rtc::binary& data);
    void process_data_chunks(std::vector<char> &&chunk);

    //finalizers
    void process_block_footer(std::vector<char> &&footer_data);
    void process_file_eof();

    //Utility
    void send_ack(bool accept , uint64_t resume_block = 0);

public:
    FileReceiver(std::shared_ptr<rtc::DataChannel> data_channel , std::string download_dir , bool skip_existing);

    ~FileReceiver();

    void start_receiving();
};
