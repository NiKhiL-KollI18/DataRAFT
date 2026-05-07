
#include "globals.h"
#include "receiver.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;
using ui = UIManager;

void FileReceiver::handle_password_auth() {

    std::thread([this]() {
    string input_password;

    bool auth_success = false;
    vector<uint8_t> salt(manifest_.crypto_salt_ , manifest_.crypto_salt_ + 16);

    char safe_hash[128] = {0};
    memcpy(safe_hash , manifest_.password_hash_sha256_ , sizeof(manifest_.password_hash_sha256_));

    string expected_hash(safe_hash);
    bool is_valid = true;

    ui::print(Level::SYSTEM , "Transfer is locked.");
    ui::new_line();

    while (!auth_success && raft_globals::is_running) {

        if (is_valid == true) {
        input_password = ui::prompt_input("Enter password: ");
        }else {
        input_password = ui::prompt_input("\033[A\r\033[KIncorrect password. Try again: ");
        }

        if (!raft_globals::is_running) break;

        try {
            file_helper::StreamDecryptor temp_decrypt(input_password , salt);

            if (temp_decrypt.check_password_hash(expected_hash)) {

            ui::print(Level::INFO , "[Receiver] Password accepted.");
            password_ = input_password;

            decryptor_.emplace(input_password, salt);
            auth_success = true;
            }else {
                is_valid = false;
            }
        } catch (const std::exception& e) {
            raft_globals::shutdown(Level::ERR , string("[Receiver] Cryptographic Error: ")+ e.what());
        }
    }

    if (raft_globals::is_running) {
        send_ack(true , 0);
        current_state_ = ReceiverState::AWAITING_METADATA;
    }
    }).detach();
}