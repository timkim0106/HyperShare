#include "hypershare/core/cli.hpp"
#include "hypershare/core/utils.hpp"
#include <iostream>
#include <iomanip>

namespace hypershare::core {

CommandLineParser::CommandLineParser(const std::string& program_name) 
    : program_name_(program_name) {
    
    add_option("h", "help", "Show this help message");
    add_option("v", "version", "Show version information");
    add_option("c", "config", "Configuration file path", true, "~/.hypershare.conf");
    add_option("", "verbose", "Enable verbose logging");
}

void CommandLineParser::add_option(const std::string& short_name, const std::string& long_name, 
                                  const std::string& description, bool has_value, 
                                  const std::string& default_value) {
    Option option{long_name, description, has_value, default_value};
    
    if (!long_name.empty()) {
        options_[long_name] = option;
    }
    
    if (!short_name.empty()) {
        short_to_long_[short_name] = long_name;
        if (long_name.empty()) {
            options_[short_name] = option;
        }
    }
}

bool CommandLineParser::parse(int argc, char* argv[]) {
    positional_args_.clear();
    parsed_options_.clear();
    error_.clear();
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg.starts_with("--")) {
            auto eq_pos = arg.find('=');
            std::string option_name = arg.substr(2, eq_pos - 2);
            
            auto it = options_.find(option_name);
            if (it == options_.end()) {
                error_ = "Unknown option: --" + option_name;
                return false;
            }
            
            if (it->second.has_value) {
                if (eq_pos != std::string::npos) {
                    parsed_options_[option_name] = arg.substr(eq_pos + 1);
                } else if (i + 1 < argc) {
                    parsed_options_[option_name] = argv[++i];
                } else {
                    error_ = "Option --" + option_name + " requires a value";
                    return false;
                }
            } else {
                parsed_options_[option_name] = "true";
            }
        }
        else if (arg.starts_with("-") && arg.length() > 1) {
            for (size_t j = 1; j < arg.length(); ++j) {
                std::string short_opt(1, arg[j]);
                
                auto long_it = short_to_long_.find(short_opt);
                if (long_it == short_to_long_.end()) {
                    error_ = "Unknown option: -" + short_opt;
                    return false;
                }
                
                std::string long_name = long_it->second;
                auto opt_it = options_.find(long_name);
                
                if (opt_it->second.has_value) {
                    if (j == arg.length() - 1) {
                        if (i + 1 < argc) {
                            parsed_options_[long_name] = argv[++i];
                        } else {
                            error_ = "Option -" + short_opt + " requires a value";
                            return false;
                        }
                    } else {
                        parsed_options_[long_name] = arg.substr(j + 1);
                        break;
                    }
                } else {
                    parsed_options_[long_name] = "true";
                }
            }
        }
        else {
            positional_args_.push_back(arg);
        }
    }
    
    return true;
}

bool CommandLineParser::has_option(const std::string& name) const {
    std::string normalized = normalize_option_name(name);
    return parsed_options_.find(normalized) != parsed_options_.end();
}

std::string CommandLineParser::get_option(const std::string& name, const std::string& default_value) const {
    std::string normalized = normalize_option_name(name);
    auto it = parsed_options_.find(normalized);
    if (it != parsed_options_.end()) {
        return it->second;
    }
    
    auto opt_it = options_.find(normalized);
    if (opt_it != options_.end() && !opt_it->second.default_value.empty()) {
        return opt_it->second.default_value;
    }
    
    return default_value;
}

int CommandLineParser::get_int_option(const std::string& name, int default_value) const {
    auto value = get_option(name);
    if (value.empty()) return default_value;
    
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

bool CommandLineParser::get_bool_option(const std::string& name, bool default_value) const {
    if (!has_option(name)) return default_value;
    
    auto value = utils::StringUtils::to_lower(get_option(name));
    return value == "true" || value == "1" || value == "yes" || value.empty();
}

void CommandLineParser::print_help() const {
    std::cout << "Usage: " << program_name_ << " [options] <command> [args...]\n\n";
    std::cout << "Options:\n";
    
    for (const auto& [name, option] : options_) {
        std::string short_opt;
        for (const auto& [short_name, long_name] : short_to_long_) {
            if (long_name == name) {
                short_opt = "-" + short_name + ", ";
                break;
            }
        }
        
        std::cout << "  " << std::left << std::setw(20) 
                  << (short_opt + "--" + name + (option.has_value ? " <value>" : ""))
                  << option.description;
        
        if (!option.default_value.empty()) {
            std::cout << " (default: " << option.default_value << ")";
        }
        std::cout << "\n";
    }
    
    std::cout << "\nCommands:\n";
    std::cout << "  start     Start HyperShare daemon\n";
    std::cout << "  share     Share a file\n";
    std::cout << "  connect   Connect to a peer\n";
    std::cout << "  status    Show current status\n";
    std::cout << "  peers     List connected peers\n";
}

void CommandLineParser::print_version() const {
    std::cout << program_name_ << " version 1.0.0\n";
    std::cout << "Built with C++20\n";
}

std::string CommandLineParser::normalize_option_name(const std::string& name) const {
    auto it = short_to_long_.find(name);
    return it != short_to_long_.end() ? it->second : name;
}

}