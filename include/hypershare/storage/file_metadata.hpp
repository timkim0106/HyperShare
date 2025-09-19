#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include "../crypto/crypto_types.hpp"

namespace hypershare::storage {

struct FileMetadata {
    std::string file_id;
    std::string file_hash;
    std::string filename;
    std::string file_path;
    uint64_t file_size;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point modified_at;
    std::vector<std::string> chunk_hashes;
    uint32_t chunk_size;
    uint32_t chunk_count;
    std::string file_type;
    std::string description;
    std::vector<std::string> tags;
    
    FileMetadata() = default;
    
    FileMetadata(const std::string& hash, const std::string& name, uint64_t size);
    
    std::vector<uint8_t> serialize() const;
    
    static FileMetadata deserialize(const std::vector<uint8_t>& data);
    
    void add_chunk_hash(const std::string& hash);
    
    bool is_complete() const;
    
    double progress() const;
    
    size_t total_chunks() const;
    
    uint32_t get_chunk_count() const;
    
    uint32_t get_chunk_size(size_t chunk_index) const;
    
    bool operator==(const FileMetadata& other) const;
    bool operator!=(const FileMetadata& other) const;
};

} // namespace hypershare::storage