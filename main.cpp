#include <iostream>
#include <chrono>
#include <filesystem>
#include <queue>

#include "CLI11.hpp"

#include "webrtc_client.h"
#include "receiver.h"
#include "sender.h"
#include "globals.h"

using namespace std;
namespace fs = std::filesystem;

namespace raft_globals {
    atomic<bool> is_running{true};

    void shutdown(const string& reason) {
        bool expected = true;
        if (!is_running.compare_exchange_strong(expected , false)) {
            return;
        }

        cout << "\n" << reason << endl;
        cout << "Shutting down..." << endl;
    }
}

string prompt_for_password() {
    string password;
    cout << "Enter your password to secure the transfer" << endl;
    cin >> password;
    return password;
}

int main(int argc , char** argv) {
    CLI::App app{"DataRAFT - Data Streaming System"};
    app.require_subcommand(1);

    //===============================
    //SEND 1. raft send
    //===============================
    auto send_cmd = app.add_subcommand("send" , "Send a file or folder");

    string target_path;
    send_cmd->add_option("path" , target_path , "path to the file or directory to send")->required();

    bool is_secure = false;
    send_cmd->add_flag("--secure,-s" , is_secure , "Lock the file with a password for sending");

    //===============================
    //SEND 1. raft receive
    //===============================
    auto receive_cmd = app.add_subcommand("receive" , "Receive a file or folder");

    string room_code;
    receive_cmd->add_option("code" , room_code , "The room id provided by the sender.")->required();

    string out_path = "./";//default directory
    receive_cmd->add_option("--out,--at,-o" , out_path , "Directory to store received contents");

    // ==========================================
    // PARSE THE COMMANDS
    // ==========================================

    CLI11_PARSE(app , argc , argv);

    try {
        if (send_cmd->parsed()) {
            cout << "[DataRAFT] Preparing to send : " << target_path << endl;

            // ==========================================
            // PHASE 2: QUEUE BUILDING & PATH RESOLUTION
            // ==========================================
            std::queue<std::string> pending_files;
            std::string base_directory;

            // 1. Force absolute path and normalize ALL slashes to OS standard
            fs::path target_fs = fs::absolute(target_path).lexically_normal();

            // 2. Physically strip trailing slashes to guarantee exact parent calculation
            std::string path_str = target_fs.string();
            while (!path_str.empty() && (path_str.back() == '/' || path_str.back() == '\\')) {
                path_str.pop_back();
            }
            target_fs = fs::path(path_str);

            if (!fs::exists(target_fs)) {
                throw std::runtime_error("Target path does not exist: " + target_path);
            }

            if (fs::is_regular_file(target_fs)) {
                // Single File
                pending_files.push(target_fs.string());
                base_directory = target_fs.parent_path().string();
            }
            else if (fs::is_directory(target_fs)) {
                // Directory: The parent of TestNastyFolder is exactly testingIN
                base_directory = target_fs.parent_path().string();

                for (const auto& entry : fs::recursive_directory_iterator(target_fs)) {
                    if (fs::is_regular_file(entry.status())) {
                        pending_files.push(entry.path().string());
                    }
                }
            }

            if (base_directory.empty()) base_directory = ".";

            if (pending_files.empty()) {
                throw std::runtime_error("No files found to send in the specified path.");
            }

            cout << "[DataRAFT] Found " << pending_files.size() << " file(s) to transfer." << endl;

            // ==========================================
            // PROCEED WITH WEBRTC HANDSHAKE
            // ==========================================
            string password;
            if (is_secure) {
                password = prompt_for_password();
            }

            WebRTCClient webrtc_client_("wss://lodge-oesc.onrender.com/signal");

            string generated_room_code = webrtc_client_.create_room();

            cout << "\n==============================================" << endl;
            cout << "Share the command with receiver : " << endl;
            cout << "raft receive " << generated_room_code << endl;
            cout << "\n==============================================" << endl;

            // Block until the receiver has connected...

            webrtc_client_.wait_for_peer_connection();
            auto data_channel_ = webrtc_client_.get_data_channel();
            cout << "Waiting for receiver..." << endl;

            // Instantiating the new Phase 2 Sender!
            Sender sender(pending_files, base_directory, data_channel_, is_secure, password);
            sender.start_sending();

            while (raft_globals::is_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        else if (receive_cmd->parsed()) {
            cout << "[DataRAFT] Connecting to room : {" << room_code << "}..." << endl;

            WebRTCClient webrtc_client("wss://lodge-oesc.onrender.com/signal");
            webrtc_client.join_room(room_code);

            webrtc_client.wait_for_peer_connection();
            auto data_channel_ = webrtc_client.get_data_channel();

            FileReceiver receiver(data_channel_ , out_path , true);

            receiver.start_receiving();

            while (raft_globals::is_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    } catch (exception& e) {
        raft_globals::shutdown(string("Fatal Setup Error:") + e.what()) ;
        return 1;
    }
    return 0;
}