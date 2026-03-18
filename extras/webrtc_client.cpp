//
// Created by nikhi on 3/15/2026.
//

#include "webrtc_client.h"
#include "../src/MDProtocol.h"

#include <filesystem>

using namespace std;
using namespace rtc;

webrtc_client::webrtc_client(bool is_sender, const std::string &filepath) {
    is_sender_ = is_sender;
    filepath_ = filepath;
    //if sender , grab the file
    if (is_sender_) {
        infile_.open(filepath_ , ios::binary);
        if (!infile_.is_open()) {
            cerr << "ERROR : could not open file. " << filepath_ << endl;
            return;
        }
    }
}

webrtc_client::~webrtc_client() {
    if (infile_.is_open()) infile_.close();
    if (outfile_.is_open()) outfile_.close();

    cout << "WebRTCClient shutting down." << endl;
}

void webrtc_client::start() {
    setup_webrtc(); //setup webrtc

    if (is_sender_) {
        setup_sender();
    }
    else {
        setup_receiver();
    }
}

void webrtc_client::setup_webrtc() {
    //getting public_ip from Google's STUN server
    Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    //building the engine
    peer_connection_ = make_shared<PeerConnection>(config);

    //tripwires

    //tripwire 2 : logs connection success or drops
    peer_connection_->onStateChange([](PeerConnection::State state){
        cout << "WebRTC engine state : " << state << endl;
    });

    //tripwire 3 : finish hunting for local and public IPs
    peer_connection_->onGatheringStateChange([this](PeerConnection::GatheringState state) {
        if (state == PeerConnection::GatheringState::Complete) {
            cout << "finished gathering IP addresses" << endl;

            auto disc = peer_connection_->localDescription();

            if (disc.has_value()) {
                cout << "paste this offer on the other computer : \n";
                cout << string(disc.value()) << endl;
            }
        }
    });
}
//helper function : reads the handshake from terminal
void ask_for_handshake(shared_ptr<PeerConnection> pc) {
    cout << "waiting for handshake..." << endl;
    cout << "paste SDP code here : \n" << endl;

    string handshake;
    string line;

    //keep reading until ENTER is pressed
    while (getline(cin , line) && !line.empty()) {
        handshake += line + "\n";
    }

    pc->setRemoteDescription(Description(handshake));
}

void webrtc_client::setup_sender() {
    //create a network pipeline
    data_channel_ = peer_connection_->createDataChannel("file_transferring_channel");

    //tripwires

    //tripwire 1 : pipe connection established
    data_channel_->onOpen([this]() {
       cout << "connection established! datapipe line is open" << endl;

        send_metadata();
    });

    //tripwire 2 : pipe brakes or closes
    data_channel_->onClosed([this]() {
        cout << "datapipe closed." << endl;
        transfer_complete_ = true;
    });

    //generates SDP offer and trigger onLocalDescription tripwire
    peer_connection_->setLocalDescription();

    ask_for_handshake(peer_connection_);
}

void webrtc_client::setup_receiver() {

    //tripwire 1 : when sender's network pipe arrives over network , catch it
    peer_connection_->onDataChannel([this](shared_ptr<DataChannel> incoming_channel) {
        cout << "INCOMING DATA CHANNEL CAUGHT" << endl;

        data_channel_ = incoming_channel;

        //tripwire 2 : when data is sent across the pipe
        data_channel_->onMessage([this](auto message) {
            if (holds_alternative<binary>(message)) {
                binary data = get<binary>(message);
                const byte* raw_data = data.data();
                size_t size = data.size();

                //if file isn't open yet , assume the message to be metadata
                if (!outfile_.is_open()) {
                    if (size == sizeof(filemeta)) {
                        const filemeta* meta_data = reinterpret_cast<const filemeta*>(raw_data);

                        cout << "incoming file : \n";
                        cout << "filename : " << meta_data->file_name_ << endl;
                        cout << "type : " << meta_data->extension_ << endl;
                        cout << "size : " << meta_data->file_size_ / 1024 << "KB" << endl;

                        string name = "../receiver" + string(meta_data->file_name_) + string(meta_data->extension_);
                        outfile_.open(name , ios::binary);

                        cout << "saving as : " << name << endl;
                        cout << "downloading..." << endl;
                    }
                }
                else {
                    outfile_.write(reinterpret_cast<const char*>(raw_data), size);
                }
            }
        });

        data_channel_->onClosed([this]() {
            cout << "transfer successful. datapipe closed by sender." << endl;
            transfer_complete_ = true;
        });
    });

    cout << "waiting for sender's handshake..." << endl;
    ask_for_handshake(peer_connection_);

    //generate the response to sender
    peer_connection_->setLocalDescription();

    cout << "response is generated!\n";
    cout << "paste it into the sender's terminal" << endl;
}

void webrtc_client::send_metadata() {
    filesystem::path p(filepath_);
    auto filesize = filesystem::file_size(p);
    string file_name = p.stem().string();
    string extension = p.extension().string();

    filemeta meta_data(filesize , file_name , extension);

    //send the custom protocol to receiver
    data_channel_->send(reinterpret_cast<const byte *>(&meta_data) , sizeof(meta_data));

    cout << "metadata sent : " << file_name << extension << "(" << filesize / 1024 << ")" << endl;

    //sending file_chunks
    send_file_chunks();
}

void webrtc_client::send_file_chunks() {
    cout << "starting file transfer..." << endl;

    //64KB file chunks
    constexpr int CHUNK_SIZE = 32 * 1024;
    vector<byte> buffer(CHUNK_SIZE);

    //max outbox queue size
    const uint64_t MAX_BUFFER = 128 * 1024;

    uintmax_t total_sent = 0;

    //send file in chunks
    while (infile_) {

        while (data_channel_->bufferedAmount() > MAX_BUFFER) {
            this_thread::sleep_for(chrono::seconds(1));
        }

        infile_.read(reinterpret_cast<char*>(buffer.data()), CHUNK_SIZE);
        streamsize bytes_read = infile_.gcount();

        if (bytes_read > 0) {
            while (!data_channel_->send(buffer.data(), bytes_read)) {
                this_thread::sleep_for(chrono::seconds(1));
            }

            total_sent += bytes_read;

            cout << "\rsent : " << total_sent / 1024 << " KB..." << flush;
        }
    }

    cout << "file reading finished. waiting for final chunks to send..." << endl;

    while (data_channel_->bufferedAmount() > 0) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    cout << "file transfer completed successfully." << endl;
    data_channel_->close();
}
