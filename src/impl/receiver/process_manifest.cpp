#include "globals.h"
#include "receiver.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;
using ui = UIManager;

void FileReceiver::process_manifest(const binary &data) {
    if (data.size() != sizeof(DataManifest)) {
        raft_globals::shutdown(Level::ERROR , "Received invalid manifest size. Aborting transfer.");
        send_ack(false, 0);
        return;
    }

    memcpy(&manifest_ , data.data() , sizeof(DataManifest));

    ostringstream oss;

    oss << "INCOMING TRANSFER : " << "\n";
    oss << "Sender : " << manifest_.sender_name_ << "\n";
    oss << "Batch Transfer : " << (manifest_.is_batch_directory_ ? "True" : "False") << "\n";
    oss << "Encrypted : " << (manifest_.is_encrypted_ ? "True" : "False");

    ui::print(Level::INFO , oss.str());
    oss.str(""); oss.clear();

    ui::new_line();
    string msg = string("Starting the file transfer. Transfer Mode :") +  (skip_existing_files_ ? "Skip Existing" : "Overwrite") + "...";
    ui::print(Level::INFO , msg);
    ui::new_line();

    if (manifest_.is_encrypted_) {
        current_state_ = ReceiverState::AWAITING_PASSWORD;
        handle_password_auth(); //detached thread
    }
    else {
        current_state_ = ReceiverState::AWAITING_METADATA;
        send_ack(true, 0);
    }
}