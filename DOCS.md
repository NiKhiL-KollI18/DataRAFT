## globals.h
**Purpose:** Manages the cross-platform library export macros and the thread-safe global lifecycle state of the DataRAFT engine.

### Namespace: raft_globals
**Description:** Contains the master execution flag and global teardown mechanisms. This namespace acts as the central heartbeat for all asynchronous worker threads.

### Variables
`extern std::atomic<bool> is_running`

**Description:** The master execution flag. Background threads (WebRTC loops, file chunkers, network flushes) monitor this atomic boolean to know when to gracefully terminate their execution loops.

**Notes:**

**Thread Safety:** Fully thread-safe (std::atomic). Can be safely polled or modified from any worker thread without requiring a mutex.

### Functions
`void shutdown(Level level, const std::string& reason)`

**Description:** Initiates a global, graceful teardown of the engine. It logs the termination event to the UI manager and safely flips the is_running flag to false, signaling all dependent threads to exit.

* #### Parameters:

  * `level (Level):` The severity level of the shutdown (e.g., normal exit vs. fatal error). Defined in ui_manager.h.

  * `reason (const std::string&):` A human-readable explanation for the termination, forwarded to the UI/logs.

* **Returns:** `void`

### Notes:

**Thread Safety:** Thread-safe. Can be invoked as an emergency stop from any network or file-processing thread.

**Memory Ownership:** Does not allocate or assume ownership of memory. String is passed safely by constant reference.

**Side Effects:** Mutates global atomic state. Triggers output streams via the UI Manager. Causes all active while(is_running) loops across the engine to break.

## config_manager.h

**Purpose:** Centralized static manager for application settings, handling OS-specific file paths, JSON serialization, and configuration queries.

### Class: ConfigManager

**Description:** A purely static class that maintains the runtime configuration state in memory. It abstracts away OS-level filesystem differences and handles reading/writing the local configuration state via `nlohmann::json`.

### Functions

`static void init()`

**Description:** Resolves OS-specific application data paths and loads the `config.json` file into memory. If the file does not exist, it generates and saves a default configuration.

* **Returns:** `void`

### Notes:

**Thread Safety:** Not thread-safe. Must be called exactly once by the main thread during engine startup before spawning network or disk workers.
**Side Effects:** Performs blocking disk I/O (read/write). Mutates the internal static JSON state.

---

`static void save()`

**Description:** Flushes the current in-memory JSON configuration state back to the local `config.json` disk file.

* **Returns:** `void`

### Notes:

**Thread Safety:** Not natively thread-safe. Requires external synchronization if `set()` and `save()` are invoked while other threads are polling config.
**Side Effects:** Performs blocking disk I/O (write).

---

`static std::string get(const std::string& key)`
`static std::string get_all()`

**Description:** Retrieves the serialized string representation of a specific configuration key, or the entire JSON configuration object for CLI output.

* #### Parameters:

  * `key (const std::string&):` The specific configuration dictionary key to query.

* **Returns:** `std::string`

### Notes:

**Memory Ownership:** Returns a newly allocated string copy of the requested data. Caller owns the returned string.

---

`static void set(const std::string& key, const std::string& value, bool is_default = false)`

**Description:** Updates a specific configuration key in memory and immediately flushes the updated state to the local disk file.

* #### Parameters:

  * `key (const std::string&):` The configuration key to modify.
  * `value (const std::string&):` The new string value to assign.
  * `is_default (bool):` Flag indicating if this is an internal default initialization bypass.

* **Returns:** `void`

### Notes:

**Side Effects:** Mutates internal static state and triggers a blocking disk writing via `save()`.

---

`static void reset_all()`

**Description:** Purges the current configuration, restores all values to hardcoded engine defaults, and saves the fresh state to disk.

* **Returns:** `void`

### Notes:

**Side Effects:** Destructively overwrites the internal JSON state and local disk file.

---

`static std::string get_username()`
`static uint64_t get_buffer_limit()`
`static std::string get_stun_server()`
`static std::string get_signaling_server()`
`static bool get_skip_existing()`
`static std::string get_default_download_dir()`
`static std::string get_log_filepath()`

**Description:** Strongly typed getter methods used by the WebRTC and sender/receiver engines to retrieve specific network, system, and user preference values safely.

* **Returns:** Strongly typed return (`std::string`, `uint64_t`, or `bool`) representing the parsed internal JSON value.

### Notes:

* **Thread Safety:** Read-only operations. Safe to poll from asynchronous worker threads, assuming configuration is immutable during active transfers.
**Memory Ownership:** String getters return newly allocated copies (pass-by-value) to ensure safe memory boundaries between the config manager and caller.

* `buffer_limit` parses the entered string to an unsigned 64-bit integer and must be passed with the metrics(Like KB, MB, GB). If not mentioned, Defaults to Bytes.

