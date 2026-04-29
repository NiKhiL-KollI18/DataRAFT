#include "receiver.h"
#include "globals.h"
#include "webrtc_client.h"

#include <filesystem>
#include <iostream>

using namespace std;
using namespace rtc;
namespace fs = std::filesystem;


void FileReceiver::start_receiving() {
    cout << "[Receiver] listening for incoming transfers..." << endl;

    data_channel_->onMessage([this](variant<binary , string> data) {
        if (!raft_globals::is_running) return;

        try {
            if (holds_alternative<binary>(data)) {

                auto raw_data = get<binary>(data);

                if (raw_data.empty()) return;

                switch (current_state_) {
                    case ReceiverState::AWAITING_MANIFEST:
                        process_manifest(raw_data);
                        break;

                    case ReceiverState::AWAITING_PASSWORD:
                        break;

                    case ReceiverState::AWAITING_METADATA:
                        process_metadata(raw_data);
                        break;

                    case ReceiverState::RECEIVING_DATA:
                    {
                        vector<char> chunk(reinterpret_cast<const char*>(raw_data.data()) ,
                            reinterpret_cast<const char*>(raw_data.data()) + raw_data.size());

                        //Extracting router flag
                        auto footer_flag = static_cast<PacketType>(chunk.back());
                        chunk.pop_back();

                        if (footer_flag == PacketType::RAW_DATA) {
                            //standard data packets
                            process_data_chunks(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::BLOCK_FOOTER) {
                            //Hit 8MB Block boundary! Verify this block's integrity
                            process_block_footer(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::FILE_EOF) {
                            //end of the entire file.
                            cout << "[Receiver] Found EOF footer" << endl;
                            process_file_eof();
                        }
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            raft_globals::shutdown(std::string("Receiver pipeline crashed: ") + e.what());
        }
    });
}
