//
// Created by nikhi on 3/15/2026.
//

#pragma once

#include <string>
#include<fstream>
#include<atomic>

#include "rtc/rtc.hpp"

using namespace std;
using namespace rtc;

class webrtc_client {
public:
    webrtc_client(bool is_sender , const std::string& filepath = "");

    ~webrtc_client();

    void start();

    atomic<bool> transfer_complete_{false};

private:
    //application states
    bool is_sender_;
    string filepath_;

    //file stream
    ifstream infile_;
    ofstream outfile_;

    //WebRTC pointers
    shared_ptr<PeerConnection> peer_connection_;
    shared_ptr<DataChannel> data_channel_;

    //internal logic
    void setup_webrtc();
    void setup_sender();
    void setup_receiver();

    //data handlers
    void send_metadata();
    void send_file_chunks();
    string generate_received_filepath(const string& filepath);
};
