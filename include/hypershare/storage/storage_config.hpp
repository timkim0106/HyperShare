#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

namespace hypershare::storage {

struct StorageConfig {
    std::filesystem::path download_directory;
    std::filesystem::path incomplete_directory;
    std::filesystem::path database_path;
    
    uint64_t max_storage_size = 10ULL * 1024 * 1024 * 1024; // 10GB default
    uint32_t default_chunk_size = 65536; // 64KB
    uint32_t max_concurrent_transfers = 10;
    
    bool auto_cleanup_incomplete = true;
    std::chrono::hours incomplete_cleanup_after{24}; // 24 hours
    
    bool enable_compression = false;
    bool enable_deduplication = true;
    
    StorageConfig() = default;
    
    explicit StorageConfig(const std::filesystem::path& base_dir);
    
    bool validate() const;
    
    bool create_directories() const;
    
    uint64_t get_available_space() const;
    
    bool has_sufficient_space(uint64_t required_bytes) const;
    
    std::filesystem::path get_file_path(const std::string& file_hash) const;
    
    std::filesystem::path get_incomplete_path(const std::string& file_hash) const;
    
    void set_base_directory(const std::filesystem::path& base_dir);
};

} // namespace hypershare::storage