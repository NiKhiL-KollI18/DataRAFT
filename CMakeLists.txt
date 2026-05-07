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
if(WIN32)
    # Windows: Use your local paths
    set(OPENSSL_ROOT_DIR "C:/Program Files/OpenSSL-Win64")
    set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libcrypto.lib" CACHE FILEPATH "" FORCE)
    set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/VC/x64/MD/libssl.lib" CACHE FILEPATH "" FORCE)
endif()

# Mac and Linux will find it automatically via system paths
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
        #Globals
        src/helper_impt/ui_manager_imp/ui_manager.cpp
        src/helper_impt/config_manager_impl/config_manager.cpp
        src/helper_impt/raft_globals_impl/raft_globals.cpp

        #File Helpers
        src/helper_impt/file_helper_impl/file_helper.cpp
        src/helper_impt/file_helper_impl/stream_encryptor.cpp
        src/helper_impt/file_helper_impl/stream_decryptor.cpp
        src/helper_impt/file_helper_impl/stream_compressor.cpp
        src/helper_impt/file_helper_impl/stream_decompressor.cpp
        src/helper_impt/file_helper_impl/stream_hasher.cpp

        #WebRTC
        src/impl/webrtc_client.cpp

        #Sender
        src/impl/sender/sender.cpp
        src/impl/sender/start_sending.cpp
        src/impl/sender/producer.cpp
        src/impl/sender/flush_network_queue.cpp
        src/impl/sender/load_next_batch_file.cpp

        #Receiver
        src/impl/receiver/receiver.cpp
        src/impl/receiver/handle_password_auth.cpp
        src/impl/receiver/process_block_footer.cpp
        src/impl/receiver/process_data_chunk.cpp
        src/impl/receiver/process_file_eof.cpp
        src/impl/receiver/process_manifest.cpp
        src/impl/receiver/process_metadata.cpp
        src/impl/receiver/start_receiving.cpp
)

target_compile_definitions(dataraft_core PRIVATE DATARAFT_CORE_EXPORTS)

target_include_directories(dataraft_core PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/helpers
)

target_link_libraries(dataraft_core
        PUBLIC
        datachannel
        nlohmann_json::nlohmann_json
        PRIVATE
        ws2_32
        OpenSSL::SSL
        OpenSSL::Crypto
        libzstd_static
)

# ==========================================
# 4. BUILD THE CLI EXECUTABLE
# ==========================================
add_executable(raft main.cpp)

target_link_libraries(raft PRIVATE
        dataraft_core
        CLI11::CLI11
)

# --- COPY OPENSSL DLLs TO BIN FOLDER FOR LOCAL TESTING ---
if(WIN32)
    add_custom_command(TARGET raft POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/needed_libraries/libcrypto-3-x64.dll"
            $<TARGET_FILE_DIR:raft>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/needed_libraries/libssl-3-x64.dll"
            $<TARGET_FILE_DIR:raft>
    )
endif()

# ==========================================
# 5. PACKAGING & INSTALL RULES (CPack)
# ==========================================

# Tell CMake what files to include in the final installer
install(TARGETS raft dataraft_core
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
)

if(WIN32)
    # Package the OpenSSL DLLs into the Windows installer's bin folder
    install(FILES
            "${CMAKE_SOURCE_DIR}/needed_libraries/libcrypto-3-x64.dll"
            "${CMAKE_SOURCE_DIR}/needed_libraries/libssl-3-x64.dll"
            DESTINATION bin
    )
endif()

# Configure WiX Specifics
if(WIN32)
    set(CPACK_GENERATOR "WIX")

    # 1. THE LOOK: Use a minimalist UI (No folder choosing, just 'Install')
    set(CPACK_WIX_UI_REF "WixUI_Minimal")

    # 2. THE ICON: Path to your .ico file (Create a 256x256 icon later)
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/assets/raft_logo.ico")

    # 3. PATH INJECTION:
    # This is handled via a 'Patch' in WiX for deep control.
    # We will tell CPack to always include the bin folder in PATH.
    set(CPACK_WIX_PATCH_FILE "${CMAKE_SOURCE_DIR}/wix_patch.xml")

    # 4. Custom images for the top banner and background
     set(CPACK_WIX_UI_DIALOG "${CMAKE_SOURCE_DIR}/assets/DatRAFTBanner.bmp")
endif()

# Global CPack Metadata
set(CPACK_PACKAGE_NAME "DataRAFT")
set(CPACK_PACKAGE_VENDOR "Nikhil")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_WIX_UPGRADE_GUID "C472A672-DD4D-426D-BD09-C0B8AB14AF6A")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DataRAFT")

include(CPack)