#pragma once

#include <atomic>
#include <memory>
#include<string>

#include <rtc/rtc.hpp>



class WebRTCClient {
private:
    //client state
    bool is_sender_;
    std::string filepath_;

    //WebRTC pointers
    std::shared_ptr<rtc::PeerConnection> peer_connection_;
    std::shared_ptr<rtc::DataChannel> data_channel_;

    //internal logic
    void setup_webrtc();
    void setup_sender();
    void setup_receiver();

public:
    WebRTCClient(bool is_sender , const std::string &filepath = "") {
        is_sender_ = is_sender;
        filepath_ = filepath;
    }

    std::atomic<bool> is_running_{true};

    ~WebRTCClient();

    //entry point
    void start();
};