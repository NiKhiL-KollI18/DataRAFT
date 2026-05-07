#include "ui_manager.h"

#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <chrono>
#include <fstream>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

//--Static Member initializations
mutex UIManager::display_mutex_;
ofstream UIManager::log_file_;
std::chrono::steady_clock::time_point UIManager::last_drawn_time_ = chrono::steady_clock::now();
int UIManager::log_count_ = 0;

//ANSI Escape Codes
const string RESET = "\033[0m";
const string RED = "\033[31m";
const string GREEN = "\033[32m";
const string YELLOW = "\033[33m";
const string BLUE = "\033[34m";
const string MAGENTA = "\033[35m";
const string CYAN = "\033[36m";
const string GRAY = "\033[90m";

//---PRIVATE HELPERS---

string UIManager::get_timestamp() {
    auto now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);
    tm* now_tm = localtime(&now_c);

    ostringstream oss;
    oss << put_time(now_tm , "%Y-%m-%d\t%H:%M:%S");
    return oss.str();
}

ostringstream UIManager::clear_line_stream() {
    ostringstream oss;
    oss << "\r\033[K";
    return oss;
}

string UIManager::smart_truncate(const string &raw_path , size_t max_width) {
    string file_name = fs::path(raw_path).filename().string();

    if (file_name.length() <= max_width) {
        file_name.append(max_width - file_name.length() , ' ');
        return file_name;
    }

    //too long , truncate in the middle
    size_t prefix_length = (max_width - 3) / 2;
    size_t suffix_length = (max_width - 3) - prefix_length;

    return file_name.substr(0 , prefix_length) + "..." + file_name.substr(file_name.length() - suffix_length);
}

std::string UIManager::format_file_size(uint64_t current_bytes, uint64_t total_bytes) {
    double divisor = 1.0;
    string unit = "B";

    if (total_bytes >= 1024ULL * 1024 * 1024) {
        divisor = 1024.0 * 1024.0 * 1024.0;
        unit = "GB";
    }
    else if (total_bytes >= 1024ULL * 1024) {
        divisor = 1024.0 * 1024.0;
        unit = "MB";
    }else if (total_bytes >= 1024ULL){
        divisor = 1024.0;
        unit = "KB";
    }

    ostringstream oss;
    oss << fixed << setprecision(1) << (current_bytes / divisor) << " / " << (total_bytes / divisor) << " " << unit;

    return oss.str();
}

string UIManager::format_speed(double speed_bps) {
    double divisor = 1.0;
    string unit = "bps";
    double speed_in_bits = speed_bps * 8.0;

    if (speed_in_bits >= 1024.0 * 1024.0 * 1024.0) {
        divisor = 1024.0 * 1024.0 * 1024.0;
        unit = "Gbps";
    }
    else if (speed_in_bits >= 1024.0 * 1024.0) {
        divisor = 1024.0 * 1024.0;
        unit = "Mbps";
    }else if (speed_in_bits >= 1024.0){
        divisor = 1024.0;
        unit = "Kbps";
    }
    ostringstream oss;
    oss << fixed << setprecision(1) << (speed_in_bits / divisor) << " " << unit;
    return oss.str();
}

//---PUBLIC METHODS---

void UIManager::init(const string &logfile_path) {
    log_file_.open(logfile_path , ios::binary);
    log_internals("===DataRAFT Engine Initialized===");
}

void UIManager::shutdown() {
    if (log_file_.is_open()) {
        log_internals("===DataRAFT Engine Shutdown===");
        log_file_.close();
    }
}

//---LOGGING / PRINTING

void UIManager::log_internals(const string &message) {
    lock_guard<mutex> lock(display_mutex_);
    if (log_file_.is_open()) {
        log_file_ << "[" << get_timestamp() << "] " << message << "\n";
        log_count_++;
    }
    if (log_count_ % 1 == 0)log_file_.flush();
}

void UIManager::print(Level level, const string &message) {
    {
        lock_guard<mutex> lock(display_mutex_);

        ostringstream oss = clear_line_stream(); //Erase progress bar before printing a log

        switch (level) {
            case Level::SUCCESS:
                oss << GREEN << message << RESET; break;
            case Level::WARNING:
                oss << YELLOW << message << RESET; break;
            case Level::ERR:
                oss << RED << message << RESET; break;
            case Level::SYSTEM:
                oss << message << RESET; break;
            case Level::INFO:
                oss << message << RESET; break;
            default: break;
        }
        cout << oss.str() << flush;
    }

    log_internals("[USER_FACING]" + message);
}

void UIManager::new_line() {
    lock_guard<mutex> lock(display_mutex_);
    cout << endl;
}

string UIManager::prompt_input(const string &prompt_message) {
    lock_guard<mutex> lock(display_mutex_);

    cout << "\r\033[K";
    cout << RESET << prompt_message <<" ";
    string input;
    cin >> input;

    return input;
}

void UIManager::draw_progress_bar(uint64_t current_bytes, uint64_t total_bytes,
    uint64_t curr_file, uint64_t total_files,
    const string &file_name, double speed_bps) {

    if (total_bytes == 0) return; //prevent divide by zero

    int percentage = static_cast<int> ((static_cast<double>(current_bytes) / total_bytes) * 100.0);
    percentage = clamp(percentage , 0 , 100);

    //--Throttle Drawing progress bar--
    auto current_time = chrono::steady_clock::now();

    //Only update the progress bar if either percentage changed OR 10MB has passed(For Massive files).
    //Also force an update if current_bytes == total_bytes (100% Completion)
    if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_drawn_time_).count() < 250) {
        return;
    }

    //---Update States---
    lock_guard<mutex> lock(display_mutex_); //Lock only if we passed the throttle
    last_drawn_time_ = current_time;

    //---DRAW UI---
    int bar_width = 20;
    int pos = static_cast<int>(static_cast<float>(bar_width * percentage) / 100.0f);

    string safe_name = smart_truncate(file_name , 20);
    string display_size = format_file_size(current_bytes , total_bytes);
    string display_speed = format_speed(speed_bps);

    ostringstream oss = clear_line_stream();

    //--draw the UI--

    //Block 1: The Progress Bar [=======>     ] 45%
    cout << "[" << RESET;
    for (int i = 0 ; i < bar_width ; i++) {
        if (i < pos) {
            oss << "=" << RESET;
        }else if (i == pos) {
            oss << ">" << RESET;
        }
        else {
            oss << " " << RESET;
        }
    }
    oss << "]" << RESET << std::setw(3) << percentage << "% " << GRAY << "|" << RESET << " ";

    //Block 2: Bytes (15.2 / 300.0MB)
    oss << display_size << " " << GRAY << "|" << RESET << " ";

    //Block 3: Speed (12.5Mbps)
    oss << display_speed << " " << GRAY << "|" << RESET << " ";

    //Block 4: File Count [1/5]
    oss << "[" << curr_file << "/" << total_files << "] " << GRAY << "|" << RESET << " ";

    //Block 5: File Name
    oss << safe_name;
    cout << oss.str() << flush;
}

