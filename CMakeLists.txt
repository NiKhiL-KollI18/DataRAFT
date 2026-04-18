cmake_minimum_required(VERSION 3.20)
project(DataRAFT)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")

set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libcrypto.lib" CACHE FILEPATH "" FORCE)
set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MD/libssl.lib" CACHE FILEPATH "" FORCE)

find_package(OpenSSL REQUIRED)

include(FetchContent)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)

# 1. Fetch content libdatachannel
FetchContent_Declare(
        libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.21.2
)

# 2. Fetch content zstd
FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        SOURCE_SUBDIR build/cmake
)

# 3. Fetch Content json
FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

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

include_directories(src)

add_executable(${PROJECT_NAME}
        main.cpp
        src/helper_impt/file_helper.cpp
        src/helper_impt/stream_encryptor.cpp
        src/helper_impt/stream_decryptor.cpp
        src/helper_impt/stream_compression.cpp
        src/helper_impt/stream_decompresser.cpp
        src/helper_impt/stream_hasher.cpp
        src/impl/webrtc_client.cpp
        src/impl/sender/sender.cpp
        src/impl/receiver/receiver.cpp
        src/impl/sender/producer.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/helpers
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        datachannel
        ws2_32
        OpenSSL::SSL
        OpenSSL::Crypto
        libzstd_static
        nlohmann_json::nlohmann_json
        CLI11::CLI11
)