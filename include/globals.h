#pragma once

#include <atomic>
#include <string>

namespace raft_globals {
    extern std::atomic<bool> is_running;

    void shutdown(const std::string& reason);
}