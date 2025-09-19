#include "hypershare/storage/storage_config.hpp"
#include <stdexcept>

namespace hypershare::storage {

StorageConfig::StorageConfig(const std::filesystem::path& base_dir) {
    set_base_directory(base_dir);
}

bool StorageConfig::validate() const {
    // Check if paths are valid
    if (download_directory.empty() || incomplete_directory.empty() || database_path.empty()) {
        return false;
    }
    
    // Check if paths are absolute
    if (!download_directory.is_absolute() || 
        !incomplete_directory.is_absolute() || 
        !database_path.is_absolute()) {
        return false;
    }
    
    // Check if max_storage_size is reasonable
    if (max_storage_size == 0) {
        return false;
    }
    
    // Check if chunk_size is reasonable
    if (default_chunk_size < 1024 || default_chunk_size > 1024 * 1024 * 10) { // 1KB to 10MB
        return false;
    }
    
    // Check if max_concurrent_transfers is reasonable
    if (max_concurrent_transfers == 0 || max_concurrent_transfers > 1000) {
        return false;
    }
    
    return true;
}

bool StorageConfig::create_directories() const {
    try {
        std::filesystem::create_directories(download_directory);
        std::filesystem::create_directories(incomplete_directory);
        
        // Create database directory
        auto db_dir = database_path.parent_path();
        if (!db_dir.empty()) {
            std::filesystem::create_directories(db_dir);
        }
        
        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

uint64_t StorageConfig::get_available_space() const {
    try {
        auto space_info = std::filesystem::space(download_directory);
        return space_info.available;
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool StorageConfig::has_sufficient_space(uint64_t required_bytes) const {
    uint64_t available = get_available_space();
    
    // Keep at least 100MB free after download
    uint64_t safety_margin = 100ULL * 1024 * 1024;
    
    return available > (required_bytes + safety_margin);
}

std::filesystem::path StorageConfig::get_file_path(const std::string& file_hash) const {
    // Create subdirectory based on first two characters of hash for better organization
    std::string subdir = file_hash.substr(0, 2);
    return download_directory / subdir / file_hash;
}

std::filesystem::path StorageConfig::get_incomplete_path(const std::string& file_hash) const {
    // Create subdirectory based on first two characters of hash for better organization
    std::string subdir = file_hash.substr(0, 2);
    return incomplete_directory / subdir / file_hash;
}

void StorageConfig::set_base_directory(const std::filesystem::path& base_dir) {
    download_directory = base_dir / "downloads";
    incomplete_directory = base_dir / "incomplete";
    database_path = base_dir / "hypershare.db";
}

} // namespace hypershare::storage