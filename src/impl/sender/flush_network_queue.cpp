#include "globals.h"
#include "sender.h"
#include "ui_manager.h"

using namespace std;
using ui = UIManager;

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