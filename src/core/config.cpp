#include "hypershare/core/config.hpp"
#include <algorithm>
#include <cctype>

namespace hypershare::core {

Config& Config::instance() {
    static Config instance;
    return instance;
}

bool Config::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));
        
        if (!key.empty()) {
            values_[key] = value;
        }
    }
    
    return true;
}

bool Config::save_to_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# HyperShare Configuration\n\n";
    
    for (const auto& [key, value] : values_) {
        file << key << "=" << value << "\n";
    }
    
    return true;
}

void Config::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

std::optional<std::string> Config::get(const std::string& key) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool Config::get_bool(const std::string& key, bool default_value) const {
    auto value = get(key);
    if (!value) return default_value;
    
    std::string lower = *value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "true" || lower == "1" || lower == "yes";
}

int Config::get_int(const std::string& key, int default_value) const {
    auto value = get_as<int>(key);
    return value ? *value : default_value;
}

std::string Config::get_string(const std::string& key, const std::string& default_value) const {
    auto value = get(key);
    return value ? *value : default_value;
}

void Config::set_defaults() {
    values_["server.port"] = "8080";
    values_["server.max_connections"] = "100";
    values_["discovery.enabled"] = "true";
    values_["discovery.port"] = "8081";
    values_["transfer.chunk_size"] = "65536";
    values_["transfer.max_parallel"] = "4";
    values_["log.level"] = "info";
    values_["log.file"] = "hypershare.log";
}

std::string Config::trim(const std::string& str) const {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        start++;
    }
    
    auto end = str.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    
    return std::string(start, end + 1);
}

}