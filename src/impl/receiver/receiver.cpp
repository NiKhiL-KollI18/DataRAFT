#include "receiver.h"
#include "webrtc_client.h"

#include <filesystem>
#include <utility>

using namespace std;
using namespace rtc;
namespace fs = std::filesystem;

FileReceiver::FileReceiver(shared_ptr<DataChannel> data_channel, string download_dir , bool skip_existing)
    :base_download_path_(std::move(download_dir)) , skip_existing_files_(skip_existing)
    , data_channel_(std::move(data_channel)){
    memset(&manifest_ , 0 , sizeof(DataManifest));
    memset(&metadata_ , 0 , sizeof(FileMeta));

    last_speed_calc_time_ = std::chrono::steady_clock::now();
    bytes_received_since_last_calc_ = 0;
    current_speed_bps_ = 0.0;
}

FileReceiver::~FileReceiver() {
    if (outfile_.is_open()) {
        outfile_.close();
    }
}

void FileReceiver::send_ack(bool accept, uint64_t resume_block) {
    TransferAck ack{};
    ack.accept_transfer_ = accept;
    ack.resume_from_block_ = resume_block;

    data_channel_->send(reinterpret_cast<const std::byte*>(&ack) , sizeof(TransferAck));
}
