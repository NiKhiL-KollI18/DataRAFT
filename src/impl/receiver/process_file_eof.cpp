#include <filesystem>

#include "receiver.h"
#include "globals.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;

namespace fs = std::filesystem;
using ui = UIManager;

void FileReceiver::process_file_eof() {
    outfile_.close();

    //--THE COMMIT--
    try {
        if (fs::exists(final_filepath_)) {
            fs::remove(final_filepath_);
        }

        fs::rename(current_filepath_ , final_filepath_);
        ui::log_internals("[Receiver] File transfer complete : " + string(metadata_.relative_path_));

    } catch (exception& e) {
        raft_globals::shutdown(Level::ERROR , std::string("Failed to finalize .raftpath rename: ") + e.what());
        return;
    }

    send_ack(true , 0);

    hasher_.reset();
    decompressor_.reset();

    //---Batch Loop---
    if (manifest_.is_batch_directory_ && current_file_count_ < manifest_.total_file_count_) {
        current_state_ = ReceiverState::AWAITING_METADATA;
        current_file_count_++;
    } else {
        decryptor_.reset();
        ui::print(Level::SUCCESS , "All Files in Batch Received successfully!");
        raft_globals::shutdown(Level::SYSTEM , "");
    }
}