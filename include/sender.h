#pragma once

#include <fstream>
#include <string>
#include <functional>

#include "protocol.h"
#include "file_helper.h"
#include "rtc/rtc.hpp"
#include<queue>

constexpr int MAX_QUEUE_SIZE = 16 * 1024 * 1024; //16MB
constexpr int BUCKET_SIZE = (32 * 1024) - 1;//32KB - 1 , to add footer at the end
constexpr int BLOCK_SIZE = 8 * 1024 * 1024; //8MB

class Sender {
private:
    //state & file details
    std::string base_directory_;
    std::string current_filepath_;
    std::queue<std::string> pending_files_;
    uint32_t total_files_in_batch_ = 0;

    std::string password_;
    std::ifstream infile_;
    FileMeta metadata_{};

    uint64_t resume_from_block_ = 0;

    DataManifest data_manifest_;

    //network
    std::shared_ptr<rtc::DataChannel> data_channel_;

    std::function<void()> on_transfer_complete_;

    //producer-consumer bridge
    std::queue<std::vector<char>> chunk_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    size_t current_queue_size_ = 0;
    std::atomic<bool> is_file_reading_completed_{false};

    //worker threads
    std::thread producer_thread_;

    //compressor , encryptor , hasher
    std::optional<file_helper::StreamCompressor> compressor_;
    std::optional<file_helper::StreamEncryptor> encryptor_;
    std::optional<file_helper::StreamingHasher> hasher_;

    enum class SenderState {
        INIT,
        WAITING_MANIFEST_ACK,
        WAITING_META_ACK,
        TRANSFERRING,
        DONE
    };
    std::atomic<SenderState> current_state_{SenderState::INIT};

    //internal logic
    void producer();

public:
    Sender(const std::queue<std::string> &files, const std::string &base_dir,
           const std::shared_ptr<rtc::DataChannel> &data_channel,
           bool is_encrypted, const std::string &password, std::function<void()> on_complete);

    ~Sender();

    void start_sending();
};