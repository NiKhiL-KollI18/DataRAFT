#include "receiver.h"
#include "globals.h"
#include "webrtc_client.h"
#include "ui_manager.h"

#include <filesystem>

using namespace std;
using namespace rtc;
namespace fs = std::filesystem;
using ui = UIManager;

void FileReceiver::start_receiving() {
    ui::log_internals("[Receiver] listening for incoming transfers...");

    data_channel_->onMessage([this](const variant<binary , string> &data) {
        if (!raft_globals::is_running) return;

        try {
            if (holds_alternative<binary>(data)) {

                const auto& raw_data = get<binary>(data);

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
                        //Extracting the router flag
                        auto footer_flag = static_cast<PacketType>(raw_data.back());
                        auto payload_size = raw_data.size() - 1;



                        if (footer_flag == PacketType::RAW_DATA) {
                            vector<char> chunk(payload_size);
                            memcpy(chunk.data() , raw_data.data() , payload_size);

                            //standard data packets
                            process_data_chunks(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::BLOCK_FOOTER) {
                            //Hit the 8MB Block boundary! Verify this block's integrity
                            vector<char> chunk(payload_size);
                            memcpy(chunk.data() , raw_data.data() , payload_size);

                            process_block_footer(std::move(chunk));
                        }
                        else if (footer_flag == PacketType::FILE_EOF) {
                            //end of the entire file.
                            ui::log_internals("[Receiver] Found EOF footer");
                            process_file_eof();
                        }
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            raft_globals::shutdown(Level::ERR , std::string("Receiver pipeline crashed: ") + e.what());
        }
    });
}
