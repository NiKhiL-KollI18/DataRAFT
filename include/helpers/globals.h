#pragma once

#include <atomic>
#include <string>

#include "ui_manager.h"

// --- CROSS-PLATFORM EXPORT MACRO ---
#if defined(_WIN32) || defined(_WIN64)
    // Windows requires distinction between exporting and importing
    #ifdef DATARAFT_CORE_EXPORTS
        #define DATARAFT_API __declspec(dllexport)
    #else
        #define DATARAFT_API __declspec(dllimport)
    #endif
#else
    // Linux/macOS (GCC/Clang) just use default visibility for both
    #define DATARAFT_API __attribute__((visibility("default")))
#endif
// -----------------------------------

namespace raft_globals {
    extern DATARAFT_API std::atomic<bool> is_running;

    DATARAFT_API void shutdown(Level level , const std::string& reason);
}