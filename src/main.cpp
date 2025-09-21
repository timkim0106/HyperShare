#include <iostream>
#include <string>
#include <vector>
#include "hypershare/core/logger.hpp"
#include "hypershare/core/config.hpp"
#include "hypershare/core/cli.hpp"
#include "hypershare/core/utils.hpp"
#include "hypershare/core/command_registry.hpp"

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
    
    // Initialize command registry
    hypershare::core::CommandRegistry command_registry;
    
    auto& args = parser.get_positional_args();
    if (args.empty()) {
        parser.print_help();
        command_registry.print_help();
        return 0;
    }

    std::string command = args[0];
    
    // Execute the command
    auto result = command_registry.execute_command(command, args);
    
    if (!result.success) {
        std::cerr << "Error: " << result.message << "\n";
        if (!command_registry.has_command(command)) {
            std::cout << "\nAvailable commands:\n";
            command_registry.print_help();
        }
    }
    
    return result.exit_code;
}