#pragma once

#include <fstream>
#include <string>
#include <cstring>

#include "protocol.h"
#include "file_helper.h"
#include "rtc/rtc.hpp"
#include<queue>

constexpr int BUCKET_SIZE = (32 * 1024) - 1;//32KB - 1 , to add footer at the end
constexpr int BLOCK_SIZE = 8 * 1024 * 1024; //8MB

class Sender {
private:
    //state & file details
    std::string base_directory_;
    std::string current_filepath_;
    std::queue<std::string> pending_files_;
    uint32_t total_files_in_batch_ = 0;

    uint64_t MAX_QUEUE_SIZE;

    std::string password_;
    std::ifstream infile_;
    FileMeta metadata_{};

    uint64_t resume_from_block_ = 0;

    DataManifest data_manifest_{};

    //trackers
    uint64_t global_bytes_transferred_ = 0;
    uint64_t total_bytes_sent_ = 0;
    uint64_t bytes_sent_since_last_calc_ = 0;
    double current_speed_bps_ = 0.0;
    std::chrono::steady_clock::time_point last_speed_calc_time_;

    //network
    std::shared_ptr<rtc::DataChannel> data_channel_;

    //producer-consumer bridge
    std::queue<std::vector<char>> chunk_queue_;
    std::mutex queue_mutex_;
    std::mutex send_mutex_;
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
    void flush_network_queue();

    bool load_next_batch_file();

public:
    Sender(const std::queue<std::string> &files, std::string base_dir,
           const std::shared_ptr<rtc::DataChannel> &data_channel,
           bool is_encrypted, const std::string &password);

    ~Sender();

    void start_sending();
};