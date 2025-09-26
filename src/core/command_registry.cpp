#include "hypershare/core/command_registry.hpp"
#include <iostream>
#include <iomanip>

namespace hypershare::core {

CommandRegistry::CommandRegistry() {
    register_command("start", std::make_unique<StartCommandHandler>());
    register_command("share", std::make_unique<ShareCommandHandler>());
    register_command("connect", std::make_unique<ConnectCommandHandler>());
    register_command("status", std::make_unique<StatusCommandHandler>());
    register_command("peers", std::make_unique<PeersCommandHandler>());
    register_command("download", std::make_unique<DownloadCommandHandler>());
    register_command("transfers", std::make_unique<TransfersCommandHandler>());
}

void CommandRegistry::register_command(const std::string& name, std::unique_ptr<CommandHandler> handler) {
    handlers_[name] = std::move(handler);
}

CommandResult CommandRegistry::execute_command(const std::string& command, const std::vector<std::string>& args) {
    auto it = handlers_.find(command);
    if (it == handlers_.end()) {
        return CommandResult::error("Unknown command: " + command);
    }
    
    return it->second->execute(args);
}

bool CommandRegistry::has_command(const std::string& command) const {
    return handlers_.find(command) != handlers_.end();
}

void CommandRegistry::print_help() const {
    std::cout << "\nCommands:\n";
    
    for (const auto& [name, handler] : handlers_) {
        std::cout << "  " << std::left << std::setw(15) << name 
                  << handler->get_description() << "\n";
        std::cout << "  " << std::left << std::setw(15) << " " 
                  << "Usage: " << handler->get_usage() << "\n\n";
    }
}

}