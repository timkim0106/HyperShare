include(GoogleTest)

# Unit tests
add_executable(unit_tests
    unit/test_config.cpp
    unit/test_logger.cpp
    unit/test_utils.cpp
    unit/test_protocol.cpp
    unit/test_message_handler.cpp
    unit/test_encryption.cpp
    unit/test_secure_handshake.cpp
    unit/test_security_comprehensive.cpp
    unit/test_file_transfer_framework.cpp
    # Phase 4 tests (will be enabled as we implement the classes)
    unit/test_file_storage.cpp
    unit/test_transfer_session.cpp
    # unit/test_file_protocol.cpp  # TODO: Fix API mismatch between file_protocol.hpp and protocol.hpp
    # unit/test_performance_reliability.cpp  # TODO: Fix Blake3Hasher API and ResumeManager API mismatches
)

target_link_libraries(unit_tests
    hypershare_core
    GTest::gtest_main
    GTest::gmock_main
    PkgConfig::LIBSODIUM
    SQLite::SQLite3
    spdlog::spdlog
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

gtest_discover_tests(unit_tests)

# Integration tests
add_executable(integration_tests
    integration/test_networking.cpp
    integration/test_key_rotation_integration.cpp
)

target_link_libraries(integration_tests
    hypershare_core
    GTest::gtest_main
    PkgConfig::LIBSODIUM
    SQLite::SQLite3
    spdlog::spdlog
)

target_include_directories(integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

gtest_discover_tests(integration_tests)

# Benchmarks
add_executable(network_benchmarks
    benchmarks/network_benchmarks.cpp
)

target_link_libraries(network_benchmarks
    hypershare_core
    benchmark::benchmark
    spdlog::spdlog
)

target_include_directories(network_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Crypto benchmarks
add_executable(crypto_benchmarks
    benchmarks/crypto_benchmarks.cpp
)

target_link_libraries(crypto_benchmarks
    hypershare_core
    benchmark::benchmark
    PkgConfig::LIBSODIUM
    spdlog::spdlog
)

target_include_directories(crypto_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)