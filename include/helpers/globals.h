#pragma once

#include <atomic>
#include <string>

#include "ui_manager.h"

#ifdef DATARAFT_CORE_EXPORTS
#define DATARAFT_API __declspec(dllexport)
#else
#define DATARAFT_API __declspec(dllimport)
#endif

namespace raft_globals {
    extern DATARAFT_API std::atomic<bool> is_running;

    DATARAFT_API void shutdown(Level level , const std::string& reason);
}