#include "config_manager.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <algorithm>

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

std::string ConfigManager::resolve_config_path() {
#ifdef _WIN32
    std::string base = std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".";
#else
    std::string base = std::getenv("HOME") ? std::getenv("HOME") : "."; // Fixed pointer typo
#endif

    std::string raft_dir = base + "/.dataraft";
    if (!std::filesystem::exists(raft_dir)) {
        std::filesystem::create_directories(raft_dir);
    }

    // Fixed: Save the config INSIDE the hidden folder
    return raft_dir + "/config.json";
}

std::string ConfigManager::get_os_downloads_folder() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    std::string base = home ? std::string(home) : ".";
    return base + "/Downloads";
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

            // Safety check: if the user manually deleted a key from the JSON, restore it
            bool missing_keys = false;
            json defaults;
            defaults["username"] = get_os_username();
            defaults["buffer_limit"] = "16MB";
            defaults["stun_server"] = "stun:stun.l.google.com:19302";
            defaults["signaling_url"] = "wss://lodge-oesc.onrender.com/signal";
            defaults["skip_existing"] = true;
            defaults["default_download_path"] = get_os_downloads_folder();

            for (auto& el : defaults.items()) {
                if (!curr_config_.contains(el.key())) {
                    curr_config_[el.key()] = el.value();
                    missing_keys = true;
                }
            }
            if (missing_keys) save();

        } catch (...) {
            std::cerr << "[Config] Warning: Corrupted config file detected. Restoring defaults." << std::endl;
            generate_default_config();
            save();
        }
    }
}

void ConfigManager::save() {
    std::ofstream file(config_path_);
    file << curr_config_.dump(4); // Pretty print with 4 spaces for human readability
}

// --- CLI HANDLERS ---

std::string ConfigManager::get_all() {
    return curr_config_.dump(4);
}

std::string ConfigManager::get(const std::string& key) {
    if (curr_config_.contains(key)) {
        // Return without quotes for strings so it looks clean in the terminal
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
            std::cout << "[Config] Restored " << key << " to default." << std::endl;
        } else {
            std::cout << "[Config] Error: Unknown key." << std::endl;
        }
        return;
    }

    // Type casting logic for booleans
    if (value == "true" || value == "1") {
        curr_config_[key] = true;
    } else if (value == "false" || value == "0") {
        curr_config_[key] = false;
    } else {
        curr_config_[key] = value;
    }

    save();
    std::cout << "[Config] Updated " << key << " successfully." << std::endl;
}

void ConfigManager::reset_all() {
    generate_default_config();
    save();
    std::cout << "[Config] All settings restored to factory defaults." << std::endl;
}

// --- FAST ENGINE GETTERS ---

std::string ConfigManager::get_username() { return curr_config_["username"]; }
std::string ConfigManager::get_stun_server() { return curr_config_["stun_server"]; }
std::string ConfigManager::get_signaling_server() { return curr_config_["signaling_url"]; }
bool ConfigManager::get_skip_existing() { return curr_config_["skip_existing"]; }

// This reads whatever the user has set in the JSON (or the OS default if they haven't changed it)
std::string ConfigManager::get_default_download_dir() {
    return curr_config_["default_download_path"].get<std::string>();
}

uint64_t ConfigManager::get_buffer_limit() {
    std::string val = curr_config_["buffer_limit"];

    // Remove spaces (e.g., "32 MB" -> "32MB")
    val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
    // Convert to uppercase
    std::transform(val.begin(), val.end(), val.begin(), ::toupper);

    uint64_t multiplier = 1;
    if (val.find("KB") != std::string::npos) multiplier = 1024;
    else if (val.find("MB") != std::string::npos) multiplier = 1024 * 1024;
    else if (val.find("GB") != std::string::npos) multiplier = 1024 * 1024 * 1024;

    try {
        uint64_t base = std::stoull(val);
        return base * multiplier;
    } catch (...) {
        return 16 * 1024 * 1024; // Safe fallback to 16MB
    }
}

std::string ConfigManager::get_log_filepath() {
#ifdef _WIN32
    std::string base = std::getenv("USERPROFILE")?std::getenv("USERPROFILE") : ".";
#else
    std::string base = std::getenv("HOME")?std::getenv("HOME") : ".";
#endif
    std::string log_dir = base + "/dataraft";
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }
    return log_dir + "/raft.log";
}
