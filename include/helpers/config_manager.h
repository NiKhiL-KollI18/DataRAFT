#pragma once

#include <string>
#include <nlohmann/json.hpp>

class ConfigManager {
private:
    static nlohmann::json curr_config_;
    static std::string config_path_;

    static std::string get_os_username();
    static std::string resolve_config_path();

    static std::string get_os_downloads_folder();

    static void generate_default_config();

public:
    //Core engines
    static void init();
    static void save();

    //CLI Handlers
    static std::string get_all();
    static std::string get(const std::string& key);

    static void set(const std::string& key , const std::string& value , bool is_default = false);
    static void reset_all();

    static std::string get_username();
    static uint64_t get_buffer_limit();
    static std::string get_stun_server();
    static std::string get_signaling_server();
    static bool get_skip_existing();
    static std::string get_default_download_dir();
    static std::string get_log_filepath();
};