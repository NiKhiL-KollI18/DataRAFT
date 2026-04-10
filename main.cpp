#include <iostream>
#include <chrono>

#include "CLI11.hpp"

#include "webrtc_client.h"
#include "receiver.h"
#include "sender.h"

using namespace std;

std::atomic<bool> is_running_{true};

void on_transfer_complete_() {
    cout << "[DataRAFT] Transfer completed..." << endl;
    is_running_ = false;
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

            // Block until receiver has connected...

            webrtc_client_.wait_for_peer_connection();
            auto data_channel_ = webrtc_client_.get_data_channel();
            cout << "Wating for receiver..." << endl;

            Sender sender(target_path , data_channel_ , is_secure , password , on_transfer_complete_);
            sender.start_sending();

            while (is_running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        else if (receive_cmd->parsed()) {
            cout << "[DataRAFT] Connecting to room : {" << room_code << "}..." << endl;

            WebRTCClient webrtc_client("wss://lodge-oesc.onrender.com/signal");
            webrtc_client.join_room(room_code);

            webrtc_client.wait_for_peer_connection();
            auto data_channel_ = webrtc_client.get_data_channel();

            FileReceiver receiver(data_channel_ , out_path , on_transfer_complete_);

            receiver.start_receiving();

            while (is_running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    } catch (exception& e) {
        cerr << "[DataRAFT] Fatal Error : " << e.what() << endl;
        return 1;
    }
    return 0;
}