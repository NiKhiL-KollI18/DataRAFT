#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <future>
#include <rtc/rtc.hpp>



class WebRTCClient {
private:
    //client state
    bool is_sender_{};
    std::string filepath_;
    std::string room_id_;
    std::string signaling_url_;

    //WebRTC & Network pointers
    std::shared_ptr<rtc::PeerConnection> peer_connection_;
    std::shared_ptr<rtc::DataChannel> data_channel_;
    std::shared_ptr<rtc::WebSocket> websocket_;

    //thread-blocking promise to communicate with main.cpp
    std::promise<std::string> room_promise_;
    std::promise<void> connection_promise_;

    //internal logic
    void setup_signaling();
    void setup_webrtc();
    void setup_sender();
    void setup_receiver();
    void handle_signaling_message(const std::string& message);

public:
    WebRTCClient(const std::string &signaling_url);

    ~WebRTCClient();

    std::string create_room();

    void join_room(const std::string& room_id);

    void wait_for_peer_connection();

    std::shared_ptr<rtc::DataChannel> get_data_channel();
};