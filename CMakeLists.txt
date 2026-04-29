cmake_minimum_required(VERSION 3.20)
project(DataRAFT)

# ==========================================
# 1. GLOBAL COMPILER & EXPORT SETTINGS
# ==========================================
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Route output files to standard production folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # .exe and .dll files go here
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # .lib linkers go here
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # Static libs go here

# ==========================================
# 2. DEPENDENCIES
# ==========================================
set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib" CACHE FILEPATH "" FORCE)

find_package(OpenSSL REQUIRED)

include(FetchContent)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)

# Fetch libdatachannel
FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.21.2
)

# Fetch zstd
FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        SOURCE_SUBDIR build/cmake
)

# Fetch nlohmann_json
FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# Fetch CLI11
FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.3.2
)

set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)

message(STATUS "Downloading libdatachannel...")
FetchContent_MakeAvailable(libdatachannel)
message(STATUS "Downloading zstd...")
FetchContent_MakeAvailable(zstd)
message(STATUS "Downloading nlohmann_json parser...")
FetchContent_MakeAvailable(json)
message(STATUS "Downloading CLI11...")
FetchContent_MakeAvailable(cli11)

# ==========================================
# 3. BUILD DATARAFT CORE ENGINE (DLL)
# ==========================================
add_library(dataraft_core SHARED
        src/raft_globals_impl/raft_globals.cpp
        src/helper_impt/file_helper.cpp
        src/helper_impt/stream_encryptor.cpp
        src/helper_impt/stream_decryptor.cpp
        src/helper_impt/stream_compression.cpp
        src/helper_impt/stream_decompresser.cpp
        src/helper_impt/stream_hasher.cpp
        src/impl/webrtc_client.cpp
        src/impl/sender/sender.cpp
        src/impl/receiver/receiver.cpp
        src/impl/receiver/handle_password_auth.cpp
        src/impl/receiver/process_block_footer.cpp
        src/impl/receiver/process_data_chunk.cpp
        src/impl/receiver/process_file_eof.cpp
        src/impl/receiver/process_manifest.cpp
        src/impl/receiver/process_metadata.cpp
        src/impl/receiver/start_receiving.cpp
        src/impl/sender/producer.cpp
)

target_compile_definitions(dataraft_core PRIVATE DATARAFT_CORE_EXPORTS)

# Using PUBLIC means anything that links to dataraft_core also gets these includes
target_include_directories(dataraft_core PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/helpers
)

# Link the heavy networking and crypto libraries ONLY to the core engine
target_link_libraries(dataraft_core
        PUBLIC
            datachannel
        PRIVATE
            ws2_32
            OpenSSL::SSL
            OpenSSL::Crypto
            libzstd_static
            nlohmann_json::nlohmann_json
)

# ==========================================
# 4. BUILD THE CLI EXECUTABLE
# ==========================================
add_executable(${PROJECT_NAME} main.cpp)

# Link the executable to our custom engine DLL and the CLI parser
target_link_libraries(${PROJECT_NAME} PRIVATE
        dataraft_core
        CLI11::CLI11
)