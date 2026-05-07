# DataRAFT

A peer-to-peer, zero-copy CLI data streaming engine written in C++20.

DataRAFT is designed to move massive files and deep directory trees directly between two systems with minimal latency and a flat memory footprint.

> **Note:** This document covers application usage and CLI commands. If you are looking for the internal C++ architecture, WebRTC network implementation, or the OpenSSL/Zstd data pipeline, please refer to [DOCS.md](DOCS.md).

---

## Features

### True Peer-to-Peer
DataRAFT uses WebRTC to punch through NATs and establish direct connections between machines. There are no middleman servers holding or caching your data.

### Flat Memory Profile
It chunks and streams. Whether you are sending a 10MB log file or a 100GB database dump, RAM usage stays completely flat.

### On-the-Fly Processing
Data is compressed (zstd) and encrypted (AES-GCM) in transit. 

### Built-in Resumability
Transfers are tracked at the block level using cryptographic hashes. If your connection drops at 99%, you pick up exactly where you left off. You don't start over.

### Zero-Overhead Directories
Point it at a folder, and it parses the structure, streams the data, and automatically rebuilds the entire directory tree on the receiving end without having to write massive temporary archive files to your disk before sending.

---

## Getting Started

DataRAFT is distributed as a pre-compiled native binary.

### Installation

Head to the [Releases page](https://github.com/NiKhiL-Kolli18/DataRAFT/releases) and download the latest installer for your system:

* **Windows (x64/ARM64):** Download the `.msi` and run the setup wizard.
* **Ubuntu/Debian:** Download the `.deb` and run:
  `sudo apt install ./DataRAFT-1.0.0-Linux.deb`
* **macOS (Intel/Apple Silicon):** Download the `.dmg` and drag the executable to your Applications.

---

## Usage

All interactions are handled through the `raft` CLI.

### Global Commands and Basic Usage

#### Check your installation:
```bash
raft 
```
If you got yelled at by the parser. You are in.

#### View available commands:
```bash
raft --help
```
#### Send a file or folder:

```bash
raft send /path/to/file.txt
```
Secure send:
```bash
raft send /path/to/file.txt --secure
```

> **Note:** All transfers are encrypted regardless of choice, But using secure allows you to put a specific key for application level encryption.

Behavior:

* The engine parses the file/directory metadata.

* It initializes the WebRTC signaling layer.

* It generates a secure connection token for the receiver.

#### Receive a file or folder:
```bash
raft receive <room_id>
```
> **Note:** Room IDs are randomly generated and need to be provided by the sender, and by default received file/folder is stored at your default download location that can be configured (by default your downloads folder).

Store receiving file at a specific path:
```bash
raft receive <room_id> --at /path/to/folder
```

Behavior:

* The engine negotiates the P2P connection with the sender.

* Data streams directly to the disk in real-time.

* Block-level checksums are verified on the fly.

* If a transfer drops, The sender initializing the same transfer and sharing you the room number can make the transfer resume from the last varified block (can be up to 8MB lower than your previous progress).

### Configuration commands
DataRAFT allows you to see/change the default configurations using `get/set` sub-commands.

#### get commands:
```bash
raft get
```
prints all the current configurations.
```bash
raft get <key>
````
prints the value of the specified key.

#### set commands:

```bash
raft set <key> <value>
```
sets the specified key to the specified value.
```bash
raft set --default
```
resets all configurations to their default values.

```bash
raft set <key> --default
```
resets the specified key to its default value.


##### all available keys:
```bash
username
```
Changes how your name appears for the receiver (by default your windows username).
```bash
default_download_path
```
Default download path when `--at` is not used.
```bash
buffer_limit 
```
Default buffer size for sender. Can help if your network bandwidth is not being fully used.
```bash
skip_existing
```
Skips existing files in the target folder.

```bash
stun_server , signaling_server
```
If you don't know what these are, Don't worry about them. Unless you absolutely 
know what you are doing, Don't touch them.

---

## Built With

DataRAFT is made possible by these incredible open-source projects:

* **[libdatachannel](https://github.com/paullouisageneau/libdatachannel):** WebRTC network stack and NAT traversal.
* **[OpenSSL](https://github.com/openssl/openssl):** AES-GCM cryptography and SHA-256 hashing.
* **[zstd](https://github.com/facebook/zstd):** High-performance real-time data compression.
* **[CLI11](https://github.com/CLIUtils/CLI11):** Command line argument parsing.
* **[nlohmann/json](https://github.com/nlohmann/json):** JSON parsing for configuration files.

---

## License

MIT License