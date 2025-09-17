include(GoogleTest)

# Unit tests
add_executable(unit_tests
    unit/test_config.cpp
    unit/test_logger.cpp
    unit/test_utils.cpp
    unit/test_protocol.cpp
    unit/test_message_handler.cpp
)

target_link_libraries(unit_tests
    hypershare_core
    GTest::gtest_main
    GTest::gmock_main
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
)

target_link_libraries(integration_tests
    hypershare_core
    GTest::gtest_main
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