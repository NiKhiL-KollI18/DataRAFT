#include <iostream>
#include <chrono>
#include <queue>
#include <string>

#include "CLI11.hpp"

#include "webrtc_client.h"
#include "receiver.h"
#include "sender.h"
#include "include/helpers/globals.h"
#include "config_manager.h"
#include "ui_manager.h"

using namespace std;
using ui = UIManager;
using config = ConfigManager;

int main(int argc , char** argv) {
    // 1. BOOT UP THE CONFIGURATION ENGINE
    config::init();

    ui::init(config::get_log_filepath());

    CLI::App app{"DataRAFT - Data Streaming System"};
    app.require_subcommand(1);

    //===============================
    // SEND: raft send
    //===============================
    auto send_cmd = app.add_subcommand("send" , "Send a file or folder");

    string target_path;
    send_cmd->add_option("path" , target_path , "path to the file or directory to send")->required();

    bool is_secure = false;
    send_cmd->add_flag("--secure,-s" , is_secure , "Lock the file with a password for sending");

    //===============================
    // RECEIVE: raft receive
    //===============================
    auto receive_cmd = app.add_subcommand("receive" , "Receive a file or folder");

    string room_code;
    receive_cmd->add_option("code" , room_code , "The room id provided by the sender.")->required();

    // DYNAMIC DEFAULT: Uses the JSON config (which defaults to the OS Downloads folder)
    string out_path = config::get_default_download_dir();
    receive_cmd->add_option("--out,--at,-o" , out_path , "Directory to store received contents");

    //===============================
    // SETTINGS: raft get / raft set
    //===============================
    auto get_cmd = app.add_subcommand("get", "View configuration settings");
    string get_key;
    get_cmd->add_option("key", get_key, "Specific setting to retrieve");

    auto set_cmd = app.add_subcommand("set", "Update configuration settings");
    string set_key, set_value;
    bool set_default = false;
    set_cmd->add_option("key", set_key, "The setting to change")->required();
    set_cmd->add_option("value", set_value, "The new value");
    set_cmd->add_flag("--default", set_default, "Restore setting to default");

    // ==========================================
    // PARSE THE COMMANDS
    // ==========================================

    CLI11_PARSE(app , argc , argv);

    try {
        ostringstream oss;
        // --- HANDLE CONFIGURATION COMMANDS FIRST ---
        if (get_cmd->parsed()) {
            if (get_key.empty()) {
                oss << config::get_all();
            } else {
                oss << config::get(get_key);
            }
            ui::print(Level::INFO , oss.str());
            ui::new_line();
            oss.str(""); oss.clear();
            return 0;
        }
        else if (set_cmd->parsed()) {
            if (set_default && set_key == "all") {
                config::reset_all();
            } else if (set_default) {
                config::set(set_key, "", true);
            } else {
                if (set_value.empty()) throw std::runtime_error("Must provide a value to set.");
                config::set(set_key, set_value, false);
            }
            return 0;
        }

        // --- HANDLE TRANSFER COMMANDS ---
        else if (send_cmd->parsed()) {

            oss << "Preparing to send " << target_path << "...";
            ui::print(Level::INFO , oss.str());
            ui::new_line();
            oss.str(""); oss.clear();

            std::queue<std::string> pending_files;
            std::string base_directory;

            file_helper::build_transfer_queue(target_path, pending_files, base_directory);

            oss << "Found " << pending_files.size() << " file(s) to transfer.";
            ui::print(Level::INFO , oss.str());
            ui::new_line();
            oss.str(""); oss.clear();

            string password;
            if (is_secure) {
                ui::print(Level::INFO , "Enter a password to secure the file transfer.");
                ui::new_line();
                password = ui::prompt_input("Enter password: ");
            }

            WebRTCClient webrtc_client_(ConfigManager::get_signaling_server());

            string generated_room_code = webrtc_client_.create_room();

            oss << "==============================================\n"
                << "Share the command with receiver : \n"
                << "raft receive " << generated_room_code << "\n"
                << "==============================================\n";
            ui::print(Level::INFO , oss.str());
            ui::new_line();
            oss.str(""); oss.clear();

            webrtc_client_.wait_for_peer_connection();
            auto data_channel_ = webrtc_client_.get_data_channel();

            oss << "Waiting for receiver...";
            ui::print(Level::INFO , oss.str());
            oss.str(""); oss.clear(); //don't call UiManager::new_line because we want the "connected" message to overwrite it.

            Sender sender(pending_files, base_directory, data_channel_, is_secure, password);

            oss << "Connected to receiver. Starting transfer...";
            ui::print(Level::INFO , oss.str());
            oss.str(""); oss.clear(); //don't call UiManager::new_line because we want "progress bar" to overwrite it.

            sender.start_sending();

            while (raft_globals::is_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        else if (receive_cmd->parsed()) {
            oss << "Connecting to room : {" << room_code << "}...";
            ui::print(Level::INFO , oss.str());
            ui::new_line();
            oss.str(""); oss.clear(); //don't call UiManager::new_line because we want "sender details" to overwrite it.

            WebRTCClient webrtc_client(ConfigManager::get_signaling_server());
            webrtc_client.join_room(room_code);

            webrtc_client.wait_for_peer_connection();
            auto data_channel_ = webrtc_client.get_data_channel();

            FileReceiver receiver(data_channel_ , out_path , ConfigManager::get_skip_existing());

            receiver.start_receiving();

            while (raft_globals::is_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    } catch (exception& e) {
        raft_globals::shutdown(Level::ERROR , string("Fatal Setup Error! ") + e.what());
        return 1;
    }
    return 0;
}