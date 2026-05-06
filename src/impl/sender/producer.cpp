#include "globals.h"
#include "sender.h"
#include "ui_manager.h"

using namespace std;
using namespace rtc;
using ui = UIManager;

void Sender::producer() {
    ui::log_internals("[Sender] Producer thread is active");

    total_bytes_sent_ = resume_from_block_ * BLOCK_SIZE;
    last_speed_calc_time_ = std::chrono::steady_clock::now();
    bytes_sent_since_last_calc_ = 0;
    current_speed_bps_ = 0.0;

    try {
        if (resume_from_block_ > 0) {
            infile_.seekg(static_cast<long long>(resume_from_block_) * BLOCK_SIZE , ios::beg);
            ui::log_internals("[Sender] Resuming from block " + to_string(resume_from_block_));
            global_bytes_transferred_ += resume_from_block_ * BLOCK_SIZE;
        }

        uint64_t current_block_index = resume_from_block_;

        // OUTER LOOP : 8MB BLOCKS
        while (infile_ && current_state_ == SenderState::TRANSFERRING && raft_globals::is_running) {
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

            // INNER LOOP : 32KB CHUNKS
            while (bytes_read_for_block < BLOCK_SIZE && infile_ && current_state_ == SenderState::TRANSFERRING && raft_globals::is_running) {

                size_t bytes_to_read = min(static_cast<size_t>(BUCKET_SIZE) ,
                    static_cast<size_t>(BLOCK_SIZE - bytes_read_for_block));
                vector<char> buffer(bytes_to_read);

                infile_.read(buffer.data() , static_cast<streamsize>(bytes_to_read));
                streamsize bytes_read = infile_.gcount();

                if (bytes_read == 0) break; // EOF reached

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

                // Add PacketType::RAW_DATA flag for routing
                buffer.push_back(static_cast<char>(PacketType::RAW_DATA));

                {
                    unique_lock<mutex> lock(queue_mutex_);
                    queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                        return current_queue_size_ < MAX_QUEUE_SIZE || !raft_globals::is_running;
                    });

                    if (!raft_globals::is_running) break; // Escape immediately if app is killed

                    current_queue_size_ += buffer.size();
                    chunk_queue_.push(std::move(buffer));
                }

                // WebRTC Stutter prevention Flush
                flush_network_queue();
            }

            if (!raft_globals::is_running) break;

            // --Block finalization--
            if (bytes_read_for_block > 0) {
                BlockFooter footer{};
                memset(&footer , 0 , sizeof(BlockFooter));

                footer.block_index_ = current_block_index;
                strncpy(footer.checksum_sha256_ , hasher_->get_sha256_hash().c_str() ,
                    sizeof(footer.checksum_sha256_) - 1);

                if (encryptor_) {
                    vector<uint8_t> auth_tag = encryptor_->get_auth_tag();
                    memcpy(footer.auth_tag_ , auth_tag.data() , 16);
                }

                vector<char> footer_buffer(sizeof(BlockFooter));
                memcpy(footer_buffer.data() , &footer , sizeof(BlockFooter));

                footer_buffer.push_back(static_cast<char>(PacketType::BLOCK_FOOTER));

                {
                    unique_lock<mutex> lock(queue_mutex_);
                    queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                        return current_queue_size_ < MAX_QUEUE_SIZE || !raft_globals::is_running;
                    });

                    if (!raft_globals::is_running) break;

                    current_queue_size_ += footer_buffer.size();
                    chunk_queue_.push(std::move(footer_buffer));
                }

                // Flush Network queue again
                flush_network_queue();

                current_block_index++; // Incremental math for the next 8MB loop
            }
        } // EOF of entire file (or aborted)

        if (raft_globals::is_running) {
            ui::log_internals("[Sender] Disk read completed. Sending EOF signal...");

            {
                vector<char> eof_buffer = { static_cast<char>(PacketType::FILE_EOF)};
                lock_guard<mutex> lock(queue_mutex_);
                current_queue_size_ += eof_buffer.size();
                chunk_queue_.push(std::move(eof_buffer));
            }

            // Final Network Flush
            flush_network_queue();

            is_file_reading_completed_ = true;
            ui::log_internals("[Sender] File read completed. Awaiting ACK from receiver...");
        }

    } catch (exception &e) {
        raft_globals::shutdown(Level::ERROR , string("Producer thread crashed: ") + e.what());
    }
}

void Sender::flush_network_queue() {

    if (!send_mutex_.try_lock()) {
        return;
    }

    lock_guard<mutex> master_lock(send_mutex_, std::adopt_lock);

    while (raft_globals::is_running) {
        vector<char> chunk_to_send;
        {
            lock_guard<mutex> inner_lock(queue_mutex_);
            if (chunk_queue_.empty() || data_channel_->bufferedAmount() + chunk_queue_.front().size() > (256 * 1024)) {
                break;
            }
            chunk_to_send = std::move(chunk_queue_.front());
            chunk_queue_.pop();
            current_queue_size_ -= chunk_to_send.size();
        }

        size_t chunk_size = chunk_to_send.size();

        // Strictly sequenced send!
        data_channel_->send(reinterpret_cast<const std::byte*>(chunk_to_send.data()) , chunk_size);

        // --- Progress & speed math ---
        total_bytes_sent_ += chunk_size;
        bytes_sent_since_last_calc_ += chunk_size;
        global_bytes_transferred_ += chunk_size;

        auto now = std::chrono::steady_clock::now();
        auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_calc_time_).count();

        // Recalculate speed every 500ms
        if (time_diff_ms >= 500) {
            current_speed_bps_ = static_cast<double>(bytes_sent_since_last_calc_) / (time_diff_ms / 1000.0);

            bytes_sent_since_last_calc_ = 0;
            last_speed_calc_time_ = now;
        }

        uint64_t current_file_num = total_files_in_batch_ - pending_files_.size();

        ui::draw_progress_bar(global_bytes_transferred_ , data_manifest_.total_folder_size_ ,
            current_file_num , total_files_in_batch_ , current_filepath_ , current_speed_bps_);
    }
}