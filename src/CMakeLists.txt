add_library(hypershare_core
    core/logger.cpp
    core/config.cpp
    core/utils.cpp
    core/cli.cpp
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
    Boost::system
    PkgConfig::LIBSODIUM
    SQLite::SQLite3
    spdlog::spdlog
)