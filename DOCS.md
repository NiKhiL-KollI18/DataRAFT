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

## ui_manager.h

**Purpose:** Provides a thread-safe, synchronized interface for standard console I/O, dynamic CLI progress rendering, and background file logging.

### Enum: Level

**Description:** Strongly-typed severity identifiers used to route and format log messages (e.g., standard output vs. error streams, color coding).
* **Values:** `INFO`, `SUCCESS`, `WARNING`, `ERR`, `SYSTEM`, `DEBUG`.

---

### Class: UIManager

**Description:** A purely static class that wraps standard streams (`std::cout`, `std::cerr`, `std::cin`) and a file stream (`std::ofstream`) with a global mutex. This ensures that concurrent network and file-processing threads do not produce garbled or interleaved console output.

### Functions

`static void init(const std::string& logfile_path)`

**Description:** Initializes the UI manager by opening the underlying file stream for background logging.

* #### Parameters:
  * `logfile_path (const std::string&):` Absolute or relative path to the destination `.log` file.

* **Returns:** `void`

### Notes:
**Thread Safety:** Not thread-safe. Must be called exactly once by the main thread during startup.
**Side Effects:** Opens a persistent file handle (`std::ofstream`).

---

`static void shutdown()`

**Description:** Flushes buffers and safely closes the background logging file stream.

* **Returns:** `void`

### Notes:
**Thread Safety:** Not thread-safe. Must be called by the `raft_global::shutdown(Level , std::string)` thread during the global teardown sequence.
**Side Effects:** Closes persistent file handles.

---

`static void print(Level level, const std::string& message)`

**Description:** Safely outputs a formatted message with a timestamp and severity tag to the console and simultaneously writes it to the log file.

* #### Parameters:
  * `level (Level):` The severity level, dictating the output stream (e.g., `ERR` prints the reason in Red color code) and UI formatting.
  * `message (const std::string&):` The raw string payload to log.

* **Returns:** `void`

### Notes:
**Thread Safety:** Fully thread-safe. Acquires `display_mutex_` to prevent interleaving.
**Side Effects:** Performs blocking console I/O and disk write operations. Prints in the current line `\r` and does not enter newline `\n`.

---

`static void log_internals(const std::string& message)`

**Description:** A dedicated logging channel for verbose or background engine events. Routes data directly to the log file (and potentially debug console) without cluttering the primary user-facing CLI.

* #### Parameters:
  * `message (const std::string&):` The raw internal state or error message.

* **Returns:** `void`

### Notes:
**Thread Safety:** Fully thread-safe. Acquires internal mutex.
**Side Effects:** Performs blocking disk write operations.

---

`static void draw_progress_bar(uint64_t current_bytes, uint64_t total_bytes, uint64_t curr_file, uint64_t total_files, const std::string& file_name, double speed_bps)`

**Description:** Calculates and renders a dynamic, single-line CLI progress bar using carriage returns (`\r`). It automatically truncates long filenames and formats byte counts and network speeds into human-readable units.

* #### Parameters:
  * `current_bytes (uint64_t):` Bytes processed so far for the active file/chunk.
  * `total_bytes (uint64_t):` Total size of the active payload.
  * `curr_file (uint64_t):` Index of the current file in the directory queue.
  * `total_files (uint64_t):` Total number of files in the manifest.
  * `file_name (const std::string&):` The name or relative path of the active file.
  * `speed_bps (double):` Current transfer rate in bytes-per-second.

* **Returns:** `void`

### Notes:
**Thread Safety:** Fully thread-safe. Acquires `display_mutex_`.
**Side Effects:** Overwrites the current line in `stdout`. Relies on the terminal supporting standard carriage return behavior.

---

`static void new_line()`

**Description:** Injects a clean newline character (`\n`) into the console output, breaking out of an active progress bar cycle.

* **Returns:** `void`

### Notes:
**Thread Safety:** Fully thread-safe. Acquires `display_mutex_`.

---

`static std::string prompt_input(const std::string& prompt_message)`

**Description:** Prints a prompt to the console and blocks the calling thread until the user inputs a string and presses Enter.

* #### Parameters:
  * `prompt_message (const std::string&):` The text displayed to the user prior to input.

* **Returns:** `std::string` containing the sanitized user input.

### Notes:
**Thread Safety:** Thread-safe, but highly destructive to asynchronous flow. It acquires `display_mutex_` and halts the invoking thread while awaiting `std::cin`. Should ideally only be used by the main thread prior to network initialization.
**Side Effects:** Blocks execution pending user I/O.

## protocol.h

**Purpose:** Defines the strict, fixed-size binary structs and enumerations used to serialize and deserialize the DataRAFT communication protocol over the WebRTC data channel.

### Global Notes on Memory & Alignment
* **`#pragma pack(push, 1)`:** All structs in this file are explicitly instructed to compile with 1-byte alignment (no padding). This guarantees that the memory layout is identical across different compilers and architectures (Windows, Mac, Linux), making them safe to serialize directly over the network using `memcpy`.
* **Memory Ownership:** All structures are Plain Old Data (POD). They contain no pointers or dynamically allocated memory (using fixed-size `char` arrays instead of `std::string`). They can be safely allocated on the stack.

---

### Enum: PacketType

**Description:** A 1-byte identifier prefixed to WebRTC binary messages to allow the receiving engine to multiplex the incoming byte stream.
* **Values:** * `RAW_DATA (0x00)`: The payload is an encrypted/compressed file chunk.
  * `BLOCK_FOOTER (0x01)`: The payload contains cryptographic validation data for the preceding raw chunk.
  * `FILE_EOF (0x02)`: The payload signals the safe completion of a specific file.

---

### Struct: FileMeta

**Description:** Describes the properties of an individual file within a transfer queue. Sent to the receiver immediately before streaming the file's data chunks.

#### Fields
* `file_size_ (uint64_t)`: Exact size of the file in bytes.
* `relative_path_ (char[512])`: The sanitized relative path used to reconstruct deep directory trees.
* `extension_ (char[16])`: The file extension.
* `is_compressed_ (bool)`: Flag indicating if the sender applied Zstd compression.
* `file_permissions_ (uint32_t)`: Original OS file permissions.
* `master_crypto_iv_ (uint8_t[12])`: A 12-byte cryptographically secure random Initialization Vector unique to this specific file, used as the base for chunk-level IV derivation.

---

### Struct: BlockFooter

**Description:** Appended as a discrete packet after a raw data chunk. Contains the cryptographic proofs required for the receiver to safely decrypt, authenticate, and hash the chunk.

#### Fields
* `block_index_ (uint64_t)`: The sequential index of the chunk. XORed with the file's `master_crypto_iv_` to derive the chunk's unique AES-GCM nonce.
* `auth_tag_ (uint8_t[16])`: The 16-byte AES-GCM authentication tag validating the ciphertext has not been tampered with in transit.
* `checksum_sha256_ (char[65])`: A null-terminated, hex-encoded string of the chunk's SHA-256 hash.

---

### Struct: TransferAck

**Description:** The receiver's response to an initial transfer offer. Dictates whether the connection should proceed and provides data for resumability.

#### Fields
* `accept_transfer_ (bool)`: True if the receiver consents to the download.
* `resume_from_block_ (uint64_t)`: The exact byte offset the sender should seek to before streaming. If `0`, starts a fresh transfer. If `>0`, resumes an interrupted transfer.

---

### Struct: DataManifest

**Description:** The root descriptor for an entire P2P transfer session. It defines the global scope of the payload (single file vs. batch directory) and the global cryptographic parameters.

#### Fields
* `sender_name_ (char[128])`: OS username of the sender for UI rendering.
* `is_batch_directory_ (bool)`: True if the payload is a recursive folder structure.
* `folder_name_ (char[256])`: The sanitized name of the root directory or file.
* `total_folder_size_ (uint64_t)`: Aggregate size of all files in the batch.
* `total_file_count_ (uint32_t)`: Total number of discrete files to expect.
* `is_encrypted_ (bool)`: True if the payload requires password decryption.
* `password_hash_sha256_ (char[65])`: Hex-encoded hash of the plaintext password. Used by the receiver to verify their inputted password is correct before deriving the AES key.
* `crypto_salt_ (uint8_t[16])`: The 16-byte cryptographically secure random salt used by both peers to derive the symmetric AES-256 key via PBKDF2.

---

## file_helper.h

**Purpose:** Provides a comprehensive suite of utilities for filesystem traversal, metadata extraction, cryptographic key derivation, and stateful chunk-based data processing (hashing, compression, and encryption).

### Namespace: file_helper

**Description:** Contains stateless free functions for filesystem I/O, path sanitization, and cryptographic preparation.

### Functions

`std::string to_windows_long_path(const std::string &standard_filepath)`
* **Description:** Converts a standard filesystem path into a Windows Long Path (prepends `\\?\`) to bypass the legacy 260-character MAX_PATH limit.
* **Parameters:** `standard_filepath (const std::string&)`: The original absolute path.
* **Returns:** `std::string` containing the normalized long path.
* **Notes:** Thread-safe. Performs lexical normalization.

`void extract_metadata(const std::string &filepath, const std::string &base_target_path, FileMeta& metadata)`
* **Description:** Reads disk properties (size, permissions) and dynamically reconstructs the target relative path to ensure deep directory trees are accurately recreated on the receiver's end. Populates the C-style `FileMeta` struct.
* **Parameters:**
  * `filepath (const std::string&)`: Absolute path to the local file.
  * `base_target_path (const std::string&)`: The root directory of the overall transfer batch.
  * `metadata (FileMeta&)`: Reference to the struct to be populated.
* **Returns:** `void`
* **Notes:** * **Thread Safety:** Not inherently thread-safe due to blocking disk I/O.
  * **Side Effects:** Destructively zeroes out the passed `metadata` memory before populating.

`void extract_transfer_ack(const std::string &filepath, TransferAck& response, bool accept_offer = true)`
* **Description:** Inspects the local disk for a partially downloaded file and calculates the resume byte offset, populating the `TransferAck` network response.
* **Parameters:**
  * `filepath (const std::string&)`: Path to check for an existing file.
  * `response (TransferAck&)`: Struct to populate with acceptance status and block resume offset.
  * `accept_offer (bool)`: Flag indicating if the receiver agrees to the transfer.
* **Returns:** `void`
* **Notes:** Performs blocking disk I/O to read file size.

`void create_data_manifest(DataManifest& data_manifest, const std::string &filepath, bool is_encrypted, const std::string &password_hash = "", const std::vector<uint8_t>& salt = {})`
* **Description:** Crawls a target path (file or directory tree) to aggregate the total payload size, file count, and encryption parameters, packaging them into the `DataManifest` network header.
* **Parameters:**
  * `data_manifest (DataManifest&)`: Reference to the struct to populate.
  * `filepath (const std::string&)`: The root path being transferred.
  * `is_encrypted (bool)`: Flag denoting if payload requires decryption.
  * `password_hash (const std::string&)`: SHA-256 hash of the password for receiver-side validation.
  * `salt (const std::vector<uint8_t>&)`: PBKDF2 cryptographic salt.
* **Returns:** `void`
* **Notes:** * **Side Effects:** Performs recursive, blocking disk I/O on directories. Throws `std::runtime_error` if the path does not exist.

`std::string calculate_sha256(const std::string &filepath, const std::function<void(size_t, size_t)> &progress_callback)`
* **Description:** Computes the SHA-256 checksum of an entire file on disk, firing a callback function periodically to update UI elements.
* **Returns:** `std::string` containing the hex-encoded hash.
* **Notes:** Performs heavy blocking disk I/O.

`bool is_compressible(std::string extension)`
* **Description:** Checks a file extension against a hardcoded hash set of naturally incompressible formats (e.g., `.zip`, `.mp4`, `.jpg`) to prevent wasting CPU cycles on redundant Zstd compression.
* **Returns:** `bool` (true if it should be compressed).
* **Notes:** Thread-safe.

`std::string sanitize_filename(const std::string &filepath)`
* **Description:** Strips illegal characters (`<>:"|?*` and control codes) from path strings to ensure cross-platform compatibility.
* **Returns:** `std::string` containing the sanitized path.
* **Notes:** Thread-safe.

`std::string format_file_size(uint64_t bytes)`
* **Description:** Converts raw byte counts into human-readable strings (e.g., "1.24 MiB").
* **Returns:** `std::string`
* **Notes:** Thread-safe.

`std::vector<uint8_t> derive_key(const std::string& password, const std::vector<uint8_t>& salt)`
* **Description:** Uses OpenSSL's PBKDF2_HMAC with 100,000 iterations to derive a secure 32-byte AES key from a plaintext password and salt.
* **Returns:** `std::vector<uint8_t>` containing the derived 256-bit key.
* **Notes:** Thread-safe but intentionally highly CPU-intensive. Throws on OpenSSL failure.

`std::array<uint8_t, 12> derive_block_iv(const std::vector<uint8_t> &master_iv, uint64_t block_index)`
* **Description:** Generates a unique Initialization Vector (IV) for a specific chunk by XORing the block index against the master IV. Prevents nonce-reuse in AES-GCM.
* **Returns:** `std::array<uint8_t, 12>` containing the block IV.
* **Notes:** Thread-safe.

`void build_transfer_queue(const std::string& target_path, std::queue<std::string>& pending_files, std::string& base_directory)`
* **Description:** Recursively crawls the target filesystem path and pushes all valid file paths into a processing queue.
* **Returns:** `void`
* **Notes:** Modifies queue in-place. Performs blocking directory traversal. Throws if path is invalid/empty.

---

### Class: StreamingHasher
**Description:** A stateful wrapper around the OpenSSL `EVP_MD_CTX` to securely calculate a SHA-256 hash across multiple discrete data chunks without loading the entire file into memory.

#### Methods
* **`void update_hash(const std::vector<char>& chunk)`**
  * **Description:** Ingests a chunk of bytes into the running hash calculation.
  * **Notes:** Stateful. Not thread-safe on the same instance.
* **`std::string get_sha256_hash()`**
  * **Description:** Finalizes the hash context and returns the hex-encoded string.
* **`void reset()`**
  * **Description:** Clears the internal OpenSSL context to prepare for a new file.

---

### Class: StreamCompressor
**Description:** A stateful wrapper for the Zstd compression context, optimized for real-time streaming pipelines.

#### Methods
* **`void compress_chunk(std::vector<char>& chunk, bool is_last_chunk)`**
  * **Description:** Compresses the provided data block. Uses `is_last_chunk` to properly flush the Zstd frame.
  * **Notes:** * **Side Effects:** Destructively modifies `chunk` IN-PLACE. The vector size will change to reflect the compressed payload.
    * **Thread Safety:** Stateful. Not thread-safe on the same instance.

---

### Class: StreamDecompressor
**Description:** A stateful wrapper for the Zstd decompression context.

#### Methods
* **`void decompress_chunk(std::vector<char>& chunk)`**
  * **Description:** Decompresses the provided data block.
  * **Notes:** * **Side Effects:** Destructively modifies `chunk` IN-PLACE. The vector will be resized to hold the expanded, original payload.

---

### Class: StreamEncryptor
**Description:** A stateful wrapper for OpenSSL's AES-256-GCM authenticated encryption. Handles key derivation, salt generation, and in-place ciphertext transformation.

#### Methods
* **`std::vector<uint8_t> generate_new_master_iv()`**
  * **Description:** Generates 12 bytes of cryptographically secure random data.
* **`void init_new_block(const std::array<uint8_t, 12>& block_iv)`**
  * **Description:** Re-initializes the AES cipher context with a chunk-specific IV. Must be called before encrypting a new chunk.
* **`void encrypt_chunk(std::vector<char>& chunk)`**
  * **Description:** Encrypts the plaintext data.
  * **Notes:** * **Side Effects:** Destructively modifies `chunk` IN-PLACE. Replaces plaintext bytes with ciphertext bytes.
* **`std::vector<uint8_t> get_auth_tag() const`**
  * **Description:** Retrieves the 16-byte AES-GCM authentication tag for the chunk just encrypted, ensuring tamper resistance.

---

### Class: StreamDecryptor
**Description:** A stateful wrapper for OpenSSL's AES-256-GCM decryption and authentication pipeline.

#### Methods
* **`void decrypt_chunk(std::vector<char>& chunk)`**
  * **Description:** Decrypts ciphertext back to plaintext.
  * **Notes:** * **Side Effects:** Destructively modifies `chunk` IN-PLACE.
* **`bool verify_auth_tag(const std::vector<uint8_t>& expected_tag) const`**
  * **Description:** Cryptographically compares the expected tag from the block footer against the actual tag computed during decryption.
  * **Returns:** `bool` (true if data is authentic and untampered).

---
## webrtc_client.h

**Purpose:** Encapsulates the `libdatachannel` WebRTC network stack and WebSocket signaling logic to negotiate and establish a direct, true Peer-to-Peer data connection between the sender and receiver.

### Class: WebRTCClient

**Description:** A stateful network manager that abstracts away ICE candidate gathering, NAT traversal, and SDP offer/answer negotiation via a central LODGE signaling server. It manages the lifecycle of the underlying WebRTC connection and uses standard C++ promises to synchronize asynchronous network callbacks with the main execution thread.

#### Methods

* **`WebRTCClient(const std::string &signaling_url)`**
  * **Description:** Constructor. Initializes the client state with the target signaling server URL but does not initiate network I/O.
  * **Parameters:**
    * `signaling_url (const std::string&)`: The WebSocket URL of the LODGE signaling server.
  * **Notes:**
    * *Memory Ownership:* Prepares internal `std::shared_ptr` members but defers allocation.

* **`~WebRTCClient()`**
  * **Description:** Destructor. Safely closes the WebSocket connection, WebRTC data channel, and peer connection before releasing memory.
  * **Notes:**
    * *Side Effects:* Terminates active network sockets.

* **`std::string create_room()`**
  * **Description:** Initiates the **Sender** workflow. Connects to the signaling server, requests a new connection room, and blocks the calling thread until the server responds with a generated room ID.
  * **Returns:** `std::string` containing the generated connection token (Room ID).
  * **Notes:**
    * *Thread Safety:* Thread-blocking. Halts the calling thread using `std::promise::get_future().get()` until the WebSocket callback fires.
    * *Side Effects:* Performs outgoing network I/O.

* **`void join_room(const std::string& room_id)`**
  * **Description:** Initiates the **Receiver** workflow. Connects to the signaling server and joins an existing room, which automatically triggers the SDP Offer/Answer exchange with the Sender.
  * **Parameters:**
    * `room_id (const std::string&)`: The connection token provided by the Sender.
  * **Returns:** `void`
  * **Notes:**
    * *Side Effects:* Performs outgoing network I/O. Kicks off asynchronous WebRTC negotiation callbacks.

* **`void wait_for_peer_connection()`**
  * **Description:** Halts execution until the underlying WebRTC `PeerConnection` state transitions to "Connected" and the `DataChannel` reports it is open and ready to transmit bytes.
  * **Returns:** `void`
  * **Notes:**
    * *Thread Safety:* Thread-blocking. Uses `std::future` to sleep the thread without burning CPU cycles until the network layer resolves the NAT traversal.

* **`std::shared_ptr<rtc::DataChannel> get_data_channel()`**
  * **Description:** Retrieves the active WebRTC data channel used for the actual high-throughput binary file streaming.
  * **Returns:** `std::shared_ptr<rtc::DataChannel>` pointing to the active channel.
  * **Notes:**
    * *Memory Ownership:* Returns a shared pointer, extending the lifetime of the underlying data channel to the caller (e.g., the Sender or Receiver processing loops). Thread-safe to invoke, but the channel itself must be used according to `libdatachannel` threading rules.
---
## sender.h

**Purpose:** Orchestrates the asynchronous, multi-threaded transmission of files and directory batches over the WebRTC data channel, managing the streaming pipeline (chunking, compression, encryption) and network backpressure.

### Global Constants
* `BUCKET_SIZE` (32,767 bytes): The maximum chunk size sent over the WebRTC data channel in a single packet (leaves 1 byte for the `PacketType` header).
* `BLOCK_SIZE` (8 MB): The logical grouping of chunks used to calculate intermediate SHA-256 checksums and AES-GCM tags for resumability and integrity checks.

---

### Class: Sender

**Description:** A stateful, producer-consumer engine. It utilizes a dedicated background thread (Producer) to read from disk and apply on-the-fly transformations (compression, encryption, hashing). The processed chunks are pushed into a thread-safe, bounded memory queue (`chunk_queue_`), which is then drained by the WebRTC network callbacks (Consumer) to ensure a flat memory profile.

#### Public Methods

* **`Sender(const std::queue<std::string> &files, std::string base_dir, const std::shared_ptr<rtc::DataChannel> &data_channel, bool is_encrypted, const std::string &password)`**
  * **Description:** Initializes the sender state machine, takes ownership of the file transfer queue, and binds to the active WebRTC data channel.
  * **Parameters:**
    * `files`: A pre-populated queue of absolute file paths to transfer.
    * `base_dir`: The root directory of the transfer (used to calculate relative paths).
    * `data_channel`: Shared pointer to the open WebRTC data channel.
    * `is_encrypted`: Boolean flag enabling the AES-GCM pipeline.
    * `password`: Plaintext password used to derive the cryptographic key (if encryption is enabled).
  * **Notes:**
    * *Memory Ownership:* Copies the file queue. Retains a shared reference to the data channel.
    * *Side Effects:* Does **not** start network I/O or disk reads until `start_sending()` is called.

* **`~Sender()`**
  * **Description:** Destructor. Safely joins the producer thread, releases file handles, and cleans up the processing pipeline.

* **`void start_sending()`**
  * **Description:** Kicks off the transfer state machine. Generates the global `DataManifest` and sends it over the data channel to initiate the handshake with the receiver.
  * **Returns:** `void`
  * **Notes:**
    * *Thread Safety:* Should be called from the main thread. Triggers the asynchronous WebRTC message callbacks.

#### Internal Architecture (Private Methods)

* **`void producer()`**
  * **Description:** The core disk I/O and processing loop. Runs on a dedicated background thread (`producer_thread_`). Reads the file in `BUCKET_SIZE` chunks, pushes them through the active pipeline modules (Zstd -> AES-GCM -> SHA-256), and pushes the final bytes into `chunk_queue_`.
  * **Notes:**
    * *Thread Safety:* Runs asynchronously. Uses `queue_mutex_` and `queue_cv_` to sleep the thread if the queue reaches `MAX_QUEUE_SIZE`, applying backpressure to disk reads when the network is slow.

* **`void flush_network_queue()`**
  * **Description:** The consumer loop. Triggered by `libdatachannel`'s `onBufferedAmountLow` callback. Drains `chunk_queue_` and pushes the bytes onto the network socket until the WebRTC internal buffer is full.
  * **Notes:**
    * *Thread Safety:* Called from the WebRTC internal networking thread. Acquires `send_mutex_` to prevent concurrent network writes, and `queue_mutex_` to safely pop elements. Wakes up the sleeping `producer_thread_` via `queue_cv_` when space frees up.

* **`bool load_next_batch_file()`**
  * **Description:** Transitions the engine to the next file in `pending_files_`. Closes the previous file handle, resets the compressor/encryptor/hasher contexts, generates the new `FileMeta` packet, and sends it to the receiver.
  * **Returns:** `bool` (true if a file was successfully loaded, false if the batch is complete).
  * **Notes:**
    * *Side Effects:* Performs blocking disk I/O to open `std::ifstream` and extract metadata. Modifies the internal state machine.
---

## receiver.h

**Purpose:** Manages the stateful receiving pipeline over the WebRTC data channel, reconstructing directory trees, and applying on-the-fly decryption, decompression, and cryptographic validation to incoming data streams.

### Class: FileReceiver

**Description:** A stateful network consumer driven by an internal state machine (`ReceiverState`). It listens to the WebRTC data channel, parses protocol headers, negotiates transfer resumes, and streams the processed binary payload directly to the local disk to maintain a flat memory footprint.

#### Public Methods

* **`FileReceiver(std::shared_ptr<rtc::DataChannel> data_channel, std::string download_dir, bool skip_existing)`**
  * **Description:** Initializes the receiver state machine and binds to the active WebRTC data channel.
  * **Parameters:**
    * `data_channel`: Shared pointer to the open WebRTC connection.
    * `download_dir`: The base directory where the payload/directories will be reconstructed.
    * `skip_existing`: Flag determining whether to skip or overwrite fully downloaded files.
  * **Notes:**
    * *Memory Ownership:* Retains a shared reference to the data channel.

* **`~FileReceiver()`**
  * **Description:** Destructor. Safely closes any open `std::ofstream` handles and releases cryptographic contexts.
  * **Notes:**
    * *Side Effects:* Flushes remaining buffer data to disk before destruction.

* **`void start_receiving()`**
  * **Description:** Binds the internal state machine handlers to the `libdatachannel` asynchronous `onMessage` callback and readies the engine to accept the incoming `DataManifest`.
  * **Returns:** `void`
  * **Notes:**
    * *Thread Safety:* Non-blocking. Executed on the main thread, but delegates actual processing to the WebRTC networking threads.

#### Internal Architecture & Protocol Handlers (Private Methods)

* **`void process_manifest(const rtc::binary& data)`**
  * **Description:** Handles the `AWAITING_MANIFEST` state. Parses the global transfer scope. If the manifest indicates the payload is encrypted, it transitions the state to `AWAITING_PASSWORD`. Otherwise, transitions to `AWAITING_METADATA` and sends an acceptance ACK.

* **`void handle_password_auth()`**
  * **Description:** Pauses the network state machine to query the UI Manager for a password. Derives the AES key and cryptographically compares it against the `password_hash_sha256_` in the manifest.
  * **Notes:**
    * *Thread Safety:* Thread-blocking. Halts the networking thread via standard input (`std::cin`) until the user provides the password.

* **`void process_metadata(const rtc::binary& data)`**
  * **Description:** Handles the `AWAITING_METADATA` state. Parses `FileMeta`, reconstructs any necessary subdirectories on the local disk, calculates the resume offset (if the file partially exists), and initializes the Decryptor, Decompressor, and Hasher contexts.
  * **Notes:**
    * *Side Effects:* Performs blocking disk I/O (creates directories, opens `std::ofstream`). Sends a `TransferAck` with the resume block offset.

* **`void process_data_chunks(std::vector<char> &&chunk)`**
  * **Description:** Handles the `RECEIVING_DATA` state. Ingests raw binary chunks from the network, pushes them through the active pipeline modules (AES-GCM -> Zstd -> SHA-256), and writes the final plaintext to disk.
  * **Parameters:**
    * `chunk`: The raw byte vector received from WebRTC.
  * **Notes:**
    * *Memory Ownership:* Takes an rvalue reference (`&&`). This allows the receiver to aggressively take ownership of the network buffer and modify it in-place without triggering deep copies, maximizing performance.
    * *Side Effects:* Performs blocking disk writes (`outfile_.write`).

* **`void process_block_footer(std::vector<char> &&footer_data)`**
  * **Description:** Intercepts a `BLOCK_FOOTER` packet. Extracts the expected AES-GCM authentication tag and SHA-256 checksum, comparing them against the Hasher and Decryptor's internal calculations to guarantee data integrity before the block is finalized.

* **`void process_file_eof()`**
  * **Description:** Intercepts a `FILE_EOF` packet. Safely closes the current file handle, applies the original OS file permissions, increments the batch trackers, and resets the state machine back to `AWAITING_METADATA` for the next file in the queue.
  * **Notes:**
    * *Side Effects:* Performs blocking disk I/O (modifies file permissions, closes stream).