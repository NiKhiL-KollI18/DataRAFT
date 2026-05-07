#include "config_manager.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "globals.h"
#include "ui_manager.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

json ConfigManager::curr_config_;
std::string ConfigManager::config_path_;

// --- OS UTILITIES ---

std::string ConfigManager::get_os_username() {
#ifdef _WIN32
    const char* user = std::getenv("USERNAME");
#else
    const char* user = std::getenv("USER");
#endif
    return user ? std::string(user) : "RaftUser";
}

std::string ConfigManager::get_os_raft_dir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    fs::path base = home ? fs::path(home) : fs::current_path();
    fs::path raft_dir = base / ".dataraft";

    if (!fs::exists(raft_dir)) {
        fs::create_directories(raft_dir);

#ifdef _WIN32
        // Truly hide the folder on Windows filesystem
        SetFileAttributesA(raft_dir.string().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
    }
    return raft_dir.string();
}

std::string ConfigManager::resolve_config_path() {
    fs::path raft_dir = get_os_raft_dir();
    return (raft_dir / "config.json").string();
}

std::string ConfigManager::get_os_downloads_folder() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    fs::path base = home ? fs::path(home) : fs::current_path();
    return (base / "Downloads").string();
}

std::string ConfigManager::get_log_filepath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    fs::path base = home ? fs::path(home) : fs::current_path();

    // Logs are kept in a visible folder for easy user access
    fs::path log_dir = base / "DataRAFT_Logs";
    if (!fs::exists(log_dir)) {
        fs::create_directories(log_dir);
    }
    return (log_dir / "raft.log").string();
}

// --- CORE ENGINE ---

void ConfigManager::generate_default_config() {
    curr_config_["username"] = get_os_username();
    curr_config_["buffer_limit"] = "16MB";
    curr_config_["stun_server"] = "stun:stun.l.google.com:19302";
    curr_config_["signaling_url"] = "wss://lodge-oesc.onrender.com/signal";
    curr_config_["skip_existing"] = true;
    curr_config_["default_download_path"] = get_os_downloads_folder();
}

void ConfigManager::init() {
    config_path_ = resolve_config_path();

    if (!fs::exists(config_path_)) {
        generate_default_config();
        save();
    } else {
        std::ifstream file(config_path_);
        try {
            file >> curr_config_;

            bool missing_keys = false;
            json defaults;
            defaults["username"] = get_os_username();
            defaults["buffer_limit"] = "16MB";
            defaults["stun_server"] = "stun:stun.l.google.com:19302";
            defaults["signaling_url"] = "wss://lodge-oesc.onrender.com/signal";
            defaults["skip_existing"] = true;
            defaults["default_download_path"] = get_os_downloads_folder();

            // C++17 Structured binding for clean, zero-copy iteration
            for (const auto& [key, value] : defaults.items()) {
                if (!curr_config_.contains(key)) {
                    curr_config_[key] = value;
                    missing_keys = true;
                }
            }
            if (missing_keys) save();

        } catch (...) {
            UIManager::print(Level::ERR, " Warning: Corrupted config file detected. Restoring defaults");
            UIManager::new_line();
            generate_default_config();
            save();
        }
    }
}

void ConfigManager::save() {
    std::ofstream file(config_path_);
    file << curr_config_.dump(4);
}

// --- CLI HANDLERS ---

std::string ConfigManager::get_all() {
    return curr_config_.dump(4);
}

std::string ConfigManager::get(const std::string& key) {
    if (curr_config_.contains(key)) {
        return curr_config_[key].is_string() ? curr_config_[key].get<std::string>() : curr_config_[key].dump();
    }
    return "Error: Key not found.";
}

void ConfigManager::set(const std::string& key, const std::string& value, bool is_default) {
    if (is_default) {
        json defaults;
        defaults["username"] = get_os_username();
        defaults["buffer_limit"] = "16MB";
        defaults["stun_server"] = "stun:stun.l.google.com:19302";
        defaults["signaling_url"] = "wss://lodge-oesc.onrender.com/signal";
        defaults["skip_existing"] = true;
        defaults["default_download_path"] = get_os_downloads_folder();

        if (defaults.contains(key)) {
            curr_config_[key] = defaults[key];
            save();
            UIManager::print(Level::SYSTEM, "Restored " + key + " to default: " + defaults[key].dump());
            UIManager::new_line();
        } else {
            UIManager::print(Level::ERR , "Error: Unknown key.");
            UIManager::new_line();
        }
        return;
    }

    if (value == "true" || value == "1") {
        curr_config_[key] = true;
    } else if (value == "false" || value == "0") {
        curr_config_[key] = false;
    } else {
        curr_config_[key] = value;
    }

    save();
    UIManager::print(Level::SYSTEM, "Updated " + key + " successfully.");
    UIManager::new_line();
}

void ConfigManager::reset_all() {
    generate_default_config();
    save();
    UIManager::print(Level::SYSTEM, "All settings restored to factory defaults.");
    UIManager::new_line();
}

// --- FAST ENGINE GETTERS ---

std::string ConfigManager::get_username() { return curr_config_["username"]; }
std::string ConfigManager::get_stun_server() { return curr_config_["stun_server"]; }
std::string ConfigManager::get_signaling_server() { return curr_config_["signaling_url"]; }
bool ConfigManager::get_skip_existing() { return curr_config_["skip_existing"]; }

std::string ConfigManager::get_default_download_dir() {
    return curr_config_["default_download_path"].get<std::string>();
}

uint64_t ConfigManager::get_buffer_limit() {
    std::string val = curr_config_["buffer_limit"];
    val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
    std::transform(val.begin(), val.end(), val.begin(), ::toupper);

    uint64_t multiplier = 1;
    if (val.find("KB") != std::string::npos) multiplier = 1024;
    else if (val.find("MB") != std::string::npos) multiplier = 1024 * 1024;
    else if (val.find("GB") != std::string::npos) multiplier = 1024 * 1024 * 1024;

    try {
        uint64_t base = std::stoull(val);
        return base * multiplier;
    } catch (...) {
        return 16 * 1024 * 1024;
    }
}