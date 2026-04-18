#include "sender.h"

using namespace std;
using namespace rtc;

void Sender::producer() {
    cout << "[Sender] producer thread is active. Reading from disk..." << endl;

    try {
        if (resume_from_byte > 0) {
            infile_.seekg(resume_from_byte , ios::beg);
        }

        while (infile_ && current_state_ == SenderState::TRANSFERRING) {
            vector<char> buffer(BUCKET_SIZE);

            infile_.read(buffer.data(), BUCKET_SIZE);
            streamsize bytes_read = infile_.gcount();

            if (bytes_read == 0) break;

            buffer.resize(bytes_read);
            bool is_last_chunk = (infile_.peek() == EOF || infile_.eof());

            hasher_->update_hash(buffer);

            if (compressor_) {
                compressor_->compress_chunk(buffer , is_last_chunk);
            }

            if (encryptor_) {
                encryptor_->encrypt_chunk(buffer);
            }


            //add footer 0x00 for chunk
            buffer.push_back(0x00);

            unique_lock<mutex> lock(queue_mutex_);

            queue_cv_.wait(lock , [this]() {
                return current_queue_size_ < MAX_QUEUE_SIZE;
            });

            size_t final_size = buffer.size();
            chunk_queue_.push(std::move(buffer));
            current_queue_size_ += final_size;

            // UNLOCK EXPLICITLY BEFORE NETWORK CALLS
            lock.unlock();

            // STALL PREVENTION (Safe version)
            while (true) {
                std::vector<char> chunk_to_send;
                {
                    lock_guard<mutex> inner_lock(queue_mutex_);
                    if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                        break;
                    }
                    chunk_to_send = std::move(chunk_queue_.front());
                    chunk_queue_.pop();
                    current_queue_size_ -= chunk_to_send.size();
                } // Unlocked!

                // Send outside the lock
                data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()), chunk_to_send.size());
            }
        }

        // End of File : Create Receipt Metadata
        cout << "[Sender] Disk read completed. Generating Cryptographic receipt..." << endl;

        FileMeta dummy_meta{};
        memset(&dummy_meta , 0 , sizeof(FileMeta));

        dummy_meta.is_transfer_complete_ = true;
        strncpy(dummy_meta.checksum_sha256_ , hasher_->get_sha256_hash().c_str() , sizeof(dummy_meta.checksum_sha256_) - 1);

        if (encryptor_) {
            vector<unsigned char> auth_tag = encryptor_->get_auth_tag();
            memcpy(dummy_meta.tag , auth_tag.data(), min(sizeof(dummy_meta.tag), auth_tag.size()));
        }

        vector<char> dummy_metadata(sizeof(FileMeta));
        memcpy(dummy_metadata.data(), &dummy_meta, sizeof(FileMeta));

        //append 0x01 footer for dummy meta
        dummy_metadata.push_back(0x01);

        // Safely push the final dummy metadata
        {
            lock_guard<mutex> lock(queue_mutex_);
            size_t dummy_size = dummy_metadata.size();
            chunk_queue_.push(std::move(dummy_metadata));
            current_queue_size_ += dummy_size;
        } // Unlocked!

        // Safely flush the final queue
        while (true) {
            std::vector<char> chunk_to_send;
            {
                lock_guard<mutex> inner_lock(queue_mutex_);
                if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                    break;
                }
                chunk_to_send = std::move(chunk_queue_.front());
                chunk_queue_.pop();
                current_queue_size_ -= chunk_to_send.size();
            }
            data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()), chunk_to_send.size());
        }

        is_file_reading_completed_ = true;
        cout << "[Sender] pipeline completed. Awaiting final ACK from receiver" << endl;

    } catch (const std::exception& e) {
        cout << "\n[Fatal Thread Error] Producer crashed: " << e.what() << endl;
        if (on_transfer_complete_) on_transfer_complete_();
    }
}