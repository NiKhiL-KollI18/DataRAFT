
#include "globals.h"
#include "receiver.h"

using namespace std;
using namespace rtc;

void FileReceiver::handle_password_auth() {
    std::thread([this]() {
        string input_password;
        bool auth_success = false;

        vector<uint8_t> salt(manifest_.crypto_salt_ , manifest_.crypto_salt_ + 16);

        char safe_hash[128] = {0};
        memcpy(safe_hash , manifest_.password_hash_sha256_ , sizeof(manifest_.password_hash_sha256_));
        string expected_hash(safe_hash);

        while (!auth_success && raft_globals::is_running) {
            cout << "\n[Receiver] Transfer is locked. Please Enter Password : ";
            cin >> input_password;

            if (!raft_globals::is_running) break;

            try {
                file_helper::StreamDecryptor temp_decrypt(input_password , salt);

                if (temp_decrypt.check_password_hash(expected_hash)) {
                    cout << "\n[Receiver] Password verified." << endl;
                    password_ = input_password;

                    decryptor_.emplace(input_password, salt);

                    auth_success = true;
                }else {
                    cout << "\r[Receiver] Incorrect password. Please try again." << flush;
                }
            }catch (const std::exception& e) {
                cout << "\r[Receiver] Cryptographic Error: " << e.what() << endl;
            }
        }

        if (raft_globals::is_running) {
            send_ack(true , 0);
            current_state_ = ReceiverState::AWAITING_METADATA;
        }
    }).detach();
}
