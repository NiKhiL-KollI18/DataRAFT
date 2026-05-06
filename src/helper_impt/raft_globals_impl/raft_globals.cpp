#include "globals.h"
#include "ui_manager.h"

using namespace std;


namespace raft_globals {
    atomic<bool> is_running{true};

    void shutdown(Level level , const string& reason) {
        bool expected = true;
        UIManager::shutdown();
        if (!is_running.compare_exchange_strong(expected , false)) {
            return;
        }
        UIManager::new_line();
        UIManager::print(level , reason);
        UIManager::new_line();
        UIManager::print(Level::INFO , "Shutting down...");
    }
}
