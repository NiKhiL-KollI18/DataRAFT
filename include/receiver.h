#pragma once
#include <fstream>
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

#include "file_helper.h"
#include "protocol.h"
#include "rtc/rtc.hpp"

class FileReceiver {
private:
    enum class ReceiverState {
        AWAITING_MANIFEST,
        AWAITING_PASSWORD,
        AWAITING_METADATA,
        RECEIVING_DATA,
        AWAITING_CONFIRMATION
    };

    //State & Network
    ReceiverState current_state_ = ReceiverState::AWAITING_MANIFEST;
    std::string base_download_path_;
    std::ofstream outfile_;

    std::string current_filepath_;
    std::string password_; //provided by user via CLI

    //Handshake Structs
    DataManifest manifest_;
    FileMeta metadata_;

    std::function<void()> on_transfer_complete_;

    //Progress Tracking
    uint64_t bytes_processed_count_ = 0;
    uint64_t bytes_expected_count_ = 0;
    uint64_t last_significant_point_size_ = 0;
    uint32_t current_file_count_ = 1; //start from 1

    std::shared_ptr<rtc::DataChannel> data_channel_;

    //transformers
    std::optional<file_helper::StreamDecompressor> decompressor_;
    std::optional<file_helper::StreamDecryptor> decryptor_;
    std::optional<file_helper::StreamingHasher> hasher_;


    //internal logic handlers
    void process_manifest(const rtc::binary& data);
    void handle_password_auth();
    void process_metadata(const rtc::binary& data);
    void process_data_chunks(std::vector<char>&& chunk);
    void verify_and_finalize(const rtc::binary& data);

    //Utility
    void send_ack(bool accept , uint64_t resume_pos = 0);

public:
    FileReceiver(std::shared_ptr<rtc::DataChannel> data_channel , std::string download_dir , std::function<void()> on_complete);

    ~FileReceiver();

    void start_receiving();
};
