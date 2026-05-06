#include "webrtc_client.h"
#include "globals.h"
#include "ui_manager.h"

#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace std;
using namespace rtc;

using json = nlohmann::json;
using ui = UIManager;

WebRTCClient::WebRTCClient(const string &signaling_url) {
    signaling_url_ = signaling_url;
    InitLogger(LogLevel::Warning);
}

WebRTCClient::~WebRTCClient() {
    if (websocket_) websocket_->close();
    if (peer_connection_) peer_connection_->close();
}

std::string WebRTCClient::create_room() {
    is_sender_ = true;
    setup_webrtc();
    setup_signaling();

    auto future = room_promise_.get_future();
    while (raft_globals::is_running) {
        if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
            return future.get();
        }
    }
    return "";
}

void WebRTCClient::join_room(const string &room_id) {
    is_sender_ = false;
    room_id_ = room_id;
    setup_webrtc();
    setup_signaling();
}

void WebRTCClient::wait_for_peer_connection() {
    auto future = connection_promise_.get_future();
    while (raft_globals::is_running) {
        if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
            future.get(); // Consume the void future
            return;
        }
    }
}

shared_ptr<DataChannel> WebRTCClient::get_data_channel() {
    return data_channel_;
}

void WebRTCClient::setup_signaling() {
    websocket_ = make_shared<WebSocket>();

    websocket_->onOpen([this]() {
        json msg;
        if (is_sender_) {
            msg["type"] = "create";
            msg["guest_count"] = 2;
            ui::print(Level::INFO , "Creating a room...");
            ui::log_internals("[LODGE] Requesting for room coordinates...");
        }
        else {
            msg["type"] = "join";
            msg["room_id"] = room_id_;
            ui::print(Level::INFO , "Joining room : " + room_id_);
            ui::log_internals("[LODGE] Requesting to join room : {" + room_id_ + "}...");
        }
        websocket_->send(msg.dump());
    });

    websocket_->onMessage([this](variant<binary , string> data) {
        if (holds_alternative<string>(data)) {
            handle_signaling_message(get<string>(data));
        }
    });

    websocket_->onClosed([]{});

    websocket_->onError([](const string &e) {
        raft_globals::shutdown(Level::ERROR , "WebSocket Error: " + e);
    });

    websocket_->open(signaling_url_);
}

void WebRTCClient::handle_signaling_message(const string &message) {
    try {
        json payload = json::parse(message);
        string type = payload["type"].get<string>();

        if (type == "room_created") {
            room_id_ = payload["room_id"].get<string>();
            room_promise_.set_value(room_id_);
        }
        else if (type == "error") {
            raft_globals::shutdown(Level::ERROR , "Server error: " + payload["data"].get<string>());
        }
        else if (type == "peer_joined" && is_sender_) {
            ui::log_internals("[LODGE] Peer detected! Initializing WebRTC handshake...");
            setup_sender();
        }
        else if (type == "offer" && !is_sender_) {
            string sdp = payload["data"].get<string>();
            peer_connection_->setRemoteDescription(Description(sdp , "offer"));
            setup_receiver();
            peer_connection_->setLocalDescription();
        }
        else if (type == "answer" && is_sender_) {
            string sdp = payload["data"].get<string>();
            peer_connection_->setRemoteDescription(Description(sdp , "answer"));
        }
    } catch (exception &e) {
        raft_globals::shutdown(Level::ERROR , string("[LODGE] Protocol Error: ") + e.what());
    }
}

void WebRTCClient::setup_webrtc() {
    Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    peer_connection_ = make_shared<PeerConnection>(config);

    peer_connection_->onStateChange([](PeerConnection::State state) {
        if (state == PeerConnection::State::Failed || state == PeerConnection::State::Closed) {
            raft_globals::shutdown(Level::ERROR , "WebRTC PeerConnection Failed or Closed unexpectedly.");
        }
    });

    peer_connection_->onGatheringStateChange([this](PeerConnection::GatheringState state) {
        if (state == PeerConnection::GatheringState::Complete) {
            auto desc = peer_connection_->localDescription();
            if (desc.has_value()) {
                json msg;
                msg["type"] = (is_sender_) ? "offer" : "answer";
                msg["data"] = string(desc.value());
                websocket_->send(msg.dump());
            }
        }
    });
}

void WebRTCClient::setup_sender() {
    data_channel_ = peer_connection_->createDataChannel("dataraft pipeline");

    data_channel_->onOpen([this]() {
        ui::log_internals("[WebRTC] P2P Pipe Opened! Severing LODGE connection...");
        json msg;
        msg["type"] = "leave";
        websocket_->send(msg.dump());

        connection_promise_.set_value();
    });
}

void WebRTCClient::setup_receiver() {
    peer_connection_->onDataChannel([this](shared_ptr<DataChannel> incoming_channel) {
        data_channel_ = incoming_channel;

        data_channel_->onOpen([this]() {
            ui::log_internals("[WebRTC] P2P Pipe Opened! Ready to receive...");
            json msg;
            msg["type"] = "leave";
            websocket_->send(msg.dump());

            connection_promise_.set_value();
        });
    });
}