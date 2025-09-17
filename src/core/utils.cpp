#include "hypershare/core/utils.hpp"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstdlib>

namespace hypershare::core::utils {

std::vector<std::string> StringUtils::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    
    return result;
}

std::string StringUtils::join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) return "";
    
    std::ostringstream oss;
    oss << parts[0];
    
    for (size_t i = 1; i < parts.size(); ++i) {
        oss << delimiter << parts[i];
    }
    
    return oss.str();
}

std::string StringUtils::trim(const std::string& str) {
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

std::string StringUtils::to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string StringUtils::to_upper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool StringUtils::starts_with(const std::string& str, const std::string& prefix) {
    return str.size() >= prefix.size() && 
           str.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && 
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StringUtils::format_bytes(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string StringUtils::format_duration(std::chrono::milliseconds duration) {
    auto ms = duration.count();
    
    if (ms < 1000) {
        return std::to_string(ms) + "ms";
    }
    
    auto seconds = ms / 1000;
    if (seconds < 60) {
        return std::to_string(seconds) + "s";
    }
    
    auto minutes = seconds / 60;
    seconds %= 60;
    
    if (minutes < 60) {
        return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
    }
    
    auto hours = minutes / 60;
    minutes %= 60;
    
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

bool FileUtils::exists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

bool FileUtils::is_file(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(path);
}

bool FileUtils::is_directory(const std::filesystem::path& path) {
    return std::filesystem::is_directory(path);
}

std::optional<size_t> FileUtils::file_size(const std::filesystem::path& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return std::nullopt;
    return size;
}

bool FileUtils::create_directories(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

std::optional<std::string> FileUtils::read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    return content;
}

bool FileUtils::write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << content;
    return true;
}

std::string FileUtils::get_file_extension(const std::filesystem::path& path) {
    return path.extension().string();
}

std::filesystem::path FileUtils::get_temp_dir() {
    return std::filesystem::temp_directory_path();
}

std::filesystem::path FileUtils::get_home_dir() {
    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
    return home ? std::filesystem::path(home) : std::filesystem::path(".");
}

std::chrono::system_clock::time_point TimeUtils::now() {
    return std::chrono::system_clock::now();
}

std::string TimeUtils::to_iso_string(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::optional<std::chrono::system_clock::time_point> TimeUtils::from_iso_string(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    
    if (ss.fail()) return std::nullopt;
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string TimeUtils::format_timestamp(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}