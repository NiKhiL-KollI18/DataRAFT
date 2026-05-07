#pragma once

#include <string>
#include <cstdint>
#include <mutex>

enum class Level {
    INFO,
    SUCCESS,
    WARNING,
    ERR,
    SYSTEM,
    DEBUG
};

class UIManager {
private:
    static std::mutex display_mutex_;
    static std::ofstream log_file_;

    static uint64_t total_bytes_so_far_;
    static std::chrono::steady_clock::time_point last_drawn_time_;
    static int log_count_;

    static std::ostringstream clear_line_stream();
    static std::string get_timestamp();
    static std::string smart_truncate(const std::string &raw_path , size_t max_width);
    static std::string format_file_size(uint64_t current_bytes , uint64_t total_bytes);
    static std::string format_speed(double speed_bps);

public:
    static void init(const std::string& logfile_path);

    static void shutdown();

    static void print(Level level , const std::string& message);

    static void log_internals(const std::string& message);

    static void draw_progress_bar(uint64_t current_bytes , uint64_t total_bytes ,
        uint64_t curr_file , uint64_t total_files ,
        const std::string& file_name , double speed_bps);

    static void new_line();

    static std::string prompt_input(const std::string& prompt_message);
};