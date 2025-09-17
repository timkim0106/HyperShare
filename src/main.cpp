#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "hypershare/core/logger.hpp"
#include "hypershare/core/config.hpp"
#include "hypershare/core/cli.hpp"

int main(int argc, char* argv[]) {
    hypershare::core::CommandLineParser parser("hypershare");
    
    if (!parser.parse(argc, argv)) {
        std::cerr << "Error: " << parser.get_error() << "\n\n";
        parser.print_help();
        return 1;
    }
    
    if (parser.has_option("help")) {
        parser.print_help();
        return 0;
    }
    
    if (parser.has_option("version")) {
        parser.print_version();
        return 0;
    }
    
    hypershare::core::Config::instance().set_defaults();
    
    std::string config_file = parser.get_option("config", "~/.hypershare.conf");
    if (hypershare::core::utils::FileUtils::exists(config_file)) {
        hypershare::core::Config::instance().load_from_file(config_file);
    }
    
    auto log_level = parser.has_option("verbose") ? 
        hypershare::core::LogLevel::Debug : hypershare::core::LogLevel::Info;
    hypershare::core::Logger::initialize("hypershare.log", log_level);
    
    LOG_INFO("HyperShare starting up");
    
    auto& args = parser.get_positional_args();
    if (args.empty()) {
        parser.print_help();
        return 0;
    }

    std::string command = args[0];
    
    if (command == "start") {
        auto& config = hypershare::core::Config::instance();
        int port = config.get_int("server.port", 8080);
        
        LOG_INFO("Starting HyperShare daemon on port {}", port);
        std::cout << "Starting HyperShare daemon...\n";
        std::cout << "Listening on port " << port << "\n";
        std::cout << "Press Ctrl+C to stop\n";
        
        // TODO: Implement daemon startup
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    else if (command == "share") {
        if (args.size() < 2) {
            std::cout << "Usage: hypershare share <filename>\n";
            return 1;
        }
        LOG_INFO("Sharing file: {}", args[1]);
        std::cout << "Sharing file: " << args[1] << "\n";
        // TODO: Implement file sharing
    }
    else if (command == "connect") {
        if (args.size() < 2) {
            std::cout << "Usage: hypershare connect <ip_address>\n";
            return 1;
        }
        LOG_INFO("Connecting to peer: {}", args[1]);
        std::cout << "Connecting to: " << args[1] << "\n";
        // TODO: Implement peer connection
    }
    else if (command == "status") {
        std::cout << "HyperShare Status:\n";
        std::cout << "Connected peers: 0\n";
        std::cout << "Active transfers: 0\n";
        // TODO: Implement status display
    }
    else {
        std::cout << "Unknown command: " << command << "\n";
        return 1;
    }
    
    return 0;
}