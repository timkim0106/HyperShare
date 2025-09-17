include(GoogleTest)

add_executable(unit_tests
    unit/test_config.cpp
    unit/test_logger.cpp
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