add_library(hypershare_core
    core/logger.cpp
    core/config.cpp
    core/utils.cpp
    core/cli.cpp
    network/protocol.cpp
    network/connection.cpp
    network/tcp_server.cpp
    network/tcp_client.cpp
    network/message_handler.cpp
    network/network_manager.cpp
    network/udp_discovery.cpp
    network/connection_manager.cpp
    network/secure_message.cpp
    crypto/crypto_types.cpp
    crypto/random.cpp
    crypto/key_manager.cpp
    crypto/hash.cpp
    crypto/signature.cpp
    crypto/secure_handshake.cpp
    crypto/encryption.cpp
)

target_include_directories(hypershare_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(hypershare_core
    spdlog::spdlog
)

add_executable(hypershare main.cpp)

target_link_libraries(hypershare 
    hypershare_core
    PkgConfig::LIBSODIUM
    SQLite::SQLite3
    spdlog::spdlog
)