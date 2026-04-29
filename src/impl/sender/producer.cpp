#include "sender.h"

using namespace std;
using namespace rtc;

void Sender::producer() {
    cout << "[Sender] Producer thread is active..." << endl;

    try {
        if (resume_from_block_ > 0) {
            infile_.seekg(static_cast<long long>(resume_from_block_) * BLOCK_SIZE , ios::beg);
            cout << "[Sender] Resuming from block" << endl;
        }

        uint64_t current_block_index = resume_from_block_;

        //OUTERLOOP : 8MB BLOCKS
        while (infile_ && current_state_ == SenderState::TRANSFERRING) {
            hasher_.emplace();

            if (metadata_.is_compressed_) {
                compressor_.emplace();
            }

            if (data_manifest_.is_encrypted_) {
                vector<uint8_t> master_iv_vec(metadata_.master_crypto_iv_ , metadata_.master_crypto_iv_ + 12);
                vector<uint8_t> block_iv = file_helper::derive_block_iv(master_iv_vec , current_block_index);
                encryptor_->init_new_block(block_iv);
            }

            size_t bytes_read_for_block = 0;

            //INNER LOOP : 32KB CHUNKS
            while (bytes_read_for_block < BLOCK_SIZE && infile_ && current_state_ == SenderState::TRANSFERRING) {

                size_t bytes_to_read = min(static_cast<size_t>(BUCKET_SIZE) ,
                    static_cast<size_t>(BLOCK_SIZE - bytes_read_for_block));
                vector<char> buffer(bytes_to_read);

                infile_.read(buffer.data() , static_cast<streamsize>(bytes_to_read));
                streamsize bytes_read = infile_.gcount();

                if (bytes_read == 0) break; //EOF reached

                buffer.resize(bytes_read);
                bytes_read_for_block += bytes_read;

                bool is_last_chunk_in_block = (bytes_read_for_block == BLOCK_SIZE || infile_.peek() == EOF || infile_.eof());

                hasher_->update_hash(buffer);

                if (metadata_.is_compressed_) {
                    compressor_->compress_chunk(buffer , is_last_chunk_in_block);
                }

                if (data_manifest_.is_encrypted_) {
                    encryptor_->encrypt_chunk(buffer);
                }

                //add PacketType::RAW_DATA flag for routing
                buffer.push_back(static_cast<char>(PacketType::RAW_DATA));

                //Push to Queue
                {
                    unique_lock<mutex> lock(queue_mutex_);
                    queue_cv_.wait(lock , [this]() {
                        return current_queue_size_ < MAX_QUEUE_SIZE;
                    });
                    current_queue_size_ += buffer.size();
                    chunk_queue_.push(std::move(buffer));
                }

                //WebRTC Stutter prevention Flush
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
                    data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()) , chunk_to_send.size());
                }
            }

            //--Block finalization--
            if (bytes_read_for_block > 0) {
                BlockFooter footer;
                memset(&footer , 0 , sizeof(BlockFooter));

                footer.block_index_ = current_block_index;
                strncpy(footer.checksum_sha256_ , hasher_->get_sha256_hash().c_str() ,
                    sizeof(footer.checksum_sha256_) - 1);

                if (encryptor_) {
                    vector<uint8_t> auth_tag = encryptor_->get_auth_tag();
                    memcpy(footer.auth_tag_ , auth_tag.data() , 16);
                }

                //convert struct to char vector
                vector<char> footer_buffer(sizeof(BlockFooter));
                memcpy(footer_buffer.data() , &footer , sizeof(BlockFooter));

                footer_buffer.push_back(static_cast<char>(PacketType::BLOCK_FOOTER));

                //PushFooter to queue
                {
                    unique_lock<mutex> lock(queue_mutex_);
                    queue_cv_.wait(lock , [this]() {
                        return current_queue_size_ < MAX_QUEUE_SIZE;
                    });
                    current_queue_size_ += footer_buffer.size();
                    chunk_queue_.push(std::move(footer_buffer));
                }

                //flush Network queue again
                while (true) {
                    std::vector<char> chunk_to_send;
                    {
                        lock_guard<mutex> inner_lock(queue_mutex_);
                        if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > 256 * 1024) {
                            break;
                        }
                        chunk_to_send = std::move(chunk_queue_.front());
                        chunk_queue_.pop();
                        current_queue_size_ -= chunk_to_send.size();
                    }
                    data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()) , chunk_to_send.size());
                }
                current_block_index++; //Incremental math for the next 8MB loop
            }
        }//EOF of entire file

        cout << "[Sender] Disk read completed. Sending EOF signal..." << endl;

        vector<char> eof_buffer = { static_cast<char>(PacketType::FILE_EOF)};
        {
            lock_guard<mutex> lock(queue_mutex_);
            current_queue_size_ += eof_buffer.size();
            chunk_queue_.push(std::move(eof_buffer));
        }

        //Final Network Flush
        while (true) {
            std::vector<char> chunk_to_send;
            {
                lock_guard<mutex> inner_lock(queue_mutex_);
                if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > 256 * 1024) {
                    break;
                }
                chunk_to_send = std::move(chunk_queue_.front());
                chunk_queue_.pop();
                current_queue_size_ -= chunk_to_send.size();
            }
            data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()) , chunk_to_send.size());
        }

        is_file_reading_completed_ = true;
        cout << "[Sender] File read completed. Awaiting ACK from receiver..." << endl;
    } catch (exception &e) {
        cout << "\n[Fatal Error] Producer Crashed : " << e.what() << endl;
        if (on_transfer_complete_) on_transfer_complete_();
    }
}