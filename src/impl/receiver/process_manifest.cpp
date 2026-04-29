#include "globals.h"
#include "receiver.h"

using namespace std;
using namespace rtc;

void FileReceiver::process_manifest(const binary &data) {
    if (data.size() != sizeof(DataManifest)) {
        raft_globals::shutdown("Received invalid manifest size. Aborting transfer.");
        send_ack(false, 0);
        return;
    }

    memcpy(&manifest_ , data.data() , sizeof(DataManifest));

    cout << "\n INCOMING TRANSFER : " << "\n";
    cout << "Sender : " << manifest_.sender_name_ << "\n";
    cout << "Batch Transfer : " << (manifest_.is_batch_directory_ ? "True" : "False") << "\n";
    cout << "Encrypted : " << (manifest_.is_encrypted_ ? "True" : "False") << endl;

    if (manifest_.is_encrypted_) {
        current_state_ = ReceiverState::AWAITING_PASSWORD;
        handle_password_auth(); //detached thread
    }
    else {
        current_state_ = ReceiverState::AWAITING_METADATA;
        send_ack(true, 0);
    }
}
