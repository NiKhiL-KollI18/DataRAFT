#include "receiver.h"
#include "webrtc_client.h"
using namespace std;
using namespace rtc;

FileReceiver::FileReceiver(shared_ptr<DataChannel> data_channel, string download_dir , function<void()> on_complete)
    :base_download_path_(std::move(download_dir)) , data_channel_(data_channel) , on_transfer_complete_(on_complete){
    memset(&manifest_ , 0 , sizeof(DataManifest));
    memset(&metadata_ , 0 , sizeof(FileMeta));
}

FileReceiver::~FileReceiver() {
    if (outfile_.is_open()) {
        outfile_.close();
    }
}

void FileReceiver::send_ack(bool accept, uint64_t resume_pos) {
    TransferAck ack;
    ack.accept_transfer_ = accept;
    ack.resume_from_byte_ = resume_pos;

    data_channel_->send(reinterpret_cast<const std::byte*>(&ack) , sizeof(TransferAck));
}

void FileReceiver::start_receiving() {
    cout << "[Receiver] listening for incoming transfers..." << endl;

    //state machine
    data_channel_->onMessage([this](variant<binary , string> data) {
        try {
            if (holds_alternative<binary>(data)) {

                auto raw_data = std::get<binary>(std::move(data));

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

                        //extract routing footer flag
                        uint8_t footer_flag = static_cast<uint8_t>(chunk.back());
                        chunk.pop_back();

                        if (footer_flag == 0x00) {
                            // Standard Data packet
                            process_data_chunks(std::move(chunk));
                        }
                        else if (footer_flag == 0x01) {
                            //EOF Cryptographic Receipt Payload
                            cout << "Found the EOF Footer" << endl;
                            rtc::binary receipt_data(
                                reinterpret_cast<const std::byte*>(chunk.data()) ,
                                reinterpret_cast<const std::byte*>(chunk.data()) + chunk.size()
                                );
                            verify_and_finalize(receipt_data);
                        }
                    }
                    break;

                case ReceiverState::AWAITING_CONFIRMATION:
                        verify_and_finalize(raw_data);
                        break;
                }
            }
        } catch (const std::exception& e) {
            cout << "\n[Fatal Receiver Error] Pipeline crashed: " << e.what() << endl;
            if (on_transfer_complete_) on_transfer_complete_();
        }
    });
}

void FileReceiver::process_manifest(const binary &data) {
    if (data.size() != sizeof(DataManifest)) {
        cout << "[Receiver] received invalid manifest size. aborting..." << endl;
        send_ack(false, 0);
        return;
    }

    memcpy(&manifest_ , data.data() , sizeof(DataManifest));

    cout << "\n INCOMING TRANSFER : " << "\n";
    cout << "Sender : " << manifest_.sender_name_ << "\n";
    cout << "batch Transfer : " << (manifest_.is_batch_directory_ ? "True" : "False") << "\n";
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

void FileReceiver::handle_password_auth() {
    std::thread([this]() {
        string input_password;
        bool auth_success = false;

        vector<unsigned char> salt(manifest_.crypto_salt_ , manifest_.crypto_salt_ + 16);
        vector<unsigned char> iv(manifest_.crypto_iv_ , manifest_.crypto_iv_ + 12);
        string expected_hash(manifest_.password_hash_sha256_);

        while (!auth_success) {
            cout << "\n[Receiver] Transfer is locked. Please Enter Password : ";
            cin >> input_password;

            try {
                file_helper::StreamDecryptor temp_decrypt(input_password , salt , iv);

                if (temp_decrypt.check_password_hash(expected_hash)) {
                    cout << "\n[Receiver] Password verified." << endl;
                    password_ = input_password;
                    auth_success = true;
                }else {
                    cout << "\r[Receiver] Incorrect password. Please try again." << flush;
                }
            }catch (const std::exception& e) {
                cout << "\r[Receiver] Cryptographic Error: " << e.what() << endl;
            }
        }
        send_ack(true , 0);
        current_state_ = ReceiverState::AWAITING_METADATA;
    }).detach();
}

void FileReceiver::process_metadata(const binary &data) {
    if (data.size() != sizeof(FileMeta)) {
        cout << "[Receiver] received invalid metadata size. aborting..." << endl;
        send_ack(false, 0);
        return;
    }

    memcpy(&metadata_ , data.data() , sizeof(FileMeta));

    current_filepath_ = base_download_path_ + "/" + string(metadata_.relative_path_); //append to base download path

    //resume logic & file opening
    uint64_t resume_pos = 0;
    ifstream check_file(current_filepath_ , ios::binary | ios::ate);
    if (check_file.is_open()) {
        resume_pos = check_file.tellg();
        check_file.close();
        cout << "[Receiver] Found partial file. Resuming from byte: " << resume_pos << endl;
    } else {
        cout << "[Receiver] Starting new file download." << metadata_.relative_path_ << endl;
    }

    //If resuming , open in append mode. Otherwise , truncate (create newone)
    outfile_.open(current_filepath_ , ios::binary | (resume_pos > 0 ? ios::app : ios::trunc));
    if (!outfile_.is_open()) {
        cout << "[Receiver] Could not open file for writing ->" << current_filepath_ << endl;
        send_ack(false);
        return;
    }


    bytes_processed_count_ = resume_pos;

    //reformers
    //hashing
    hasher_.emplace();

    //decryption
    if (manifest_.is_encrypted_) {
        vector<unsigned char> salt(manifest_.crypto_salt_ , manifest_.crypto_salt_ + 16);
        vector<unsigned char> iv(manifest_.crypto_iv_ , manifest_.crypto_iv_ + 12);
        decryptor_.emplace(password_ , salt , iv);
    }

    if (metadata_.is_compressed_) {
        decompressor_.emplace();
    }

    //initializing data receiver
    send_ack(true , resume_pos);
    current_state_ = ReceiverState::RECEIVING_DATA;
}

void FileReceiver::process_data_chunks(vector<char> &&chunk) {
    if (chunk.empty()) return;

    try {
        // 1. Decrypt
        if (decryptor_) decryptor_->decrypt_chunk(chunk);

        // 2.Decompress
        if (decompressor_) decompressor_->decompress_chunk(chunk);

        // 3.hash
        if (hasher_) hasher_->update_hash(chunk);

        // 4. write to disk and add to count
        outfile_.write(chunk.data(), chunk.size());

        // Ensure we flush to disk so OS buffers don't explode (Crucial for large MKV files!)
        // outfile_.flush(); // Optional: Uncomment if it crashes again to force disk writes

        bytes_processed_count_ += chunk.size();

        if (bytes_processed_count_ >= last_significant_point_size_ + 10 * 1024 * 1024) {
            cout << "\r Received : " << bytes_processed_count_ / (1024 * 1024) << "MB" << flush;
            last_significant_point_size_ = bytes_processed_count_;
        }

        // if (bytes_processed_count_ >= metadata_.file_size_) {
        //     current_state_ = ReceiverState::AWAITING_CONFIRMATION;
        // }

    } catch (const std::exception& e) {
        cout << "\n[Receiver] Pipeline Error during data processing : " << e.what() << endl;
        // Don't kill the app here, let verify_and_finalize catch the corrupted file!
    }
}

void FileReceiver::verify_and_finalize(const binary &data) {
    if (data.size() != sizeof(FileMeta)) return;

    FileMeta dummy_meta;
    memcpy(&dummy_meta, data.data() , sizeof(FileMeta));

    if (!dummy_meta.is_transfer_complete_) return;

    cout << "\n[Receiver] End of the File detected. Auditing crypto signatures..." << endl;
    bool is_valid = true;

    //verify AES-GCM Auth tag
    if (decryptor_ && current_file_count_ == manifest_.total_file_count_) {
        vector<unsigned char> expected_tag(dummy_meta.tag , dummy_meta.tag + 16);
        if (!decryptor_->verify_auth_tag(expected_tag)) {
            cout << "[Security Alert] AES-GCM Auth tag mismatch! Data maybe corrupted or tampered with" << endl;
            is_valid = false;
        }
        else {
            cout << "[Receiver] AES-GCM Authentication : PASSED" << endl;
        }
    }

    //verify SHA-256 check sum
    string calculated_hash = hasher_->get_sha256_hash();
    if (calculated_hash != dummy_meta.checksum_sha256_) {
        cout << "[SECURITY ALERT] SHA-256 checksum mismatch! File integrity may have been compromised" << endl;
        is_valid = false;
    }
    else {
        cout << "[Receiver] SHA-256 CheckSUM: passed." << endl;
    }

    if (is_valid) {
        cout << "[Receiver] Transfer successful and verified.File Stored at \n" << current_filepath_ << endl;
        send_ack(true , bytes_processed_count_);
    }
    else {
        cout << "[Receiver] Corrupted data stored at : \n" << current_filepath_ << endl;
        send_ack(false , bytes_processed_count_);
    }
    outfile_.close();
    //decryptor stays online if it's batch directory
    hasher_.reset();
    decompressor_.reset();
    if (manifest_.is_batch_directory_ && current_file_count_ < manifest_.total_file_count_) {
        current_state_ = ReceiverState::AWAITING_METADATA;
        current_file_count_++;
    }
    else {
        decryptor_.reset();
        cout << "[Receiver] All files in batch received successfully!" << endl;
        if (on_transfer_complete_) {
            on_transfer_complete_();
        }
    }
}
