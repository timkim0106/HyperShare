add_executable(hypershare main.cpp)

target_link_libraries(hypershare 
    Boost::system
    PkgConfig::LIBSODIUM
    SQLite::SQLite3
    spdlog::spdlog
)