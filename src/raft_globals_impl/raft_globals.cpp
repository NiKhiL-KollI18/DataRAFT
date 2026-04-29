#include <iostream>

#include "globals.h"

using namespace std;


namespace raft_globals {
    atomic<bool> is_running{true};

    void shutdown(const string& reason) {
        bool expected = true;
        if (!is_running.compare_exchange_strong(expected , false)) {
            return;
        }

        cout << "\n" << reason << endl;
        cout << "Shutting down..." << endl;
    }
}
