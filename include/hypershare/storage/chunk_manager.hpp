#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <cstdint>
#include <optional>
#include "storage_config.hpp"
#include "file_metadata.hpp"
#include "../crypto/crypto_types.hpp"

namespace hypershare::storage {

class ChunkManager {
public:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 65536; // 64KB
    
    ChunkManager(size_t chunk_size = DEFAULT_CHUNK_SIZE);
    explicit ChunkManager(const StorageConfig& config);
    
    std::vector<std::vector<uint8_t>> split_file(const std::filesystem::path& file_path);
    
    std::vector<std::string> get_chunk_hashes(const std::filesystem::path& file_path);
    
    hypershare::crypto::CryptoResult chunk_file(const std::string& file_path, FileMetadata& metadata);
    
    hypershare::crypto::CryptoResult write_chunk(const FileMetadata& metadata,
                                                  size_t chunk_index,
                                                  const std::vector<uint8_t>& chunk_data);
    
    hypershare::crypto::CryptoResult read_chunk(const FileMetadata& metadata,
                                                 size_t chunk_index,
                                                 std::vector<uint8_t>& chunk_data);
    
    bool write_chunk(const std::filesystem::path& base_path, 
                     const std::string& file_hash,
                     size_t chunk_index, 
                     const std::vector<uint8_t>& chunk_data);
    
    std::vector<uint8_t> read_chunk(const std::filesystem::path& base_path,
                                    const std::string& file_hash,
                                    size_t chunk_index);
    
    bool merge_chunks(const std::filesystem::path& base_path,
                      const std::string& file_hash,
                      const std::filesystem::path& output_path,
                      size_t total_chunks);
    
    bool verify_chunk(const std::vector<uint8_t>& chunk_data, 
                      const std::string& expected_hash);
    
    bool verify_chunk_hash(const std::vector<uint8_t>& chunk_data, 
                           const std::string& expected_hash);
    
    size_t get_chunk_size() const { return chunk_size_; }
    
    void set_chunk_size(size_t new_size) { chunk_size_ = new_size; }
    
    std::filesystem::path get_chunk_path(const std::filesystem::path& base_path,
                                        const std::string& file_hash,
                                        size_t chunk_index);

private:
    size_t chunk_size_;
    std::optional<StorageConfig> config_;
    
    std::string compute_chunk_hash(const std::vector<uint8_t>& chunk_data);
};

} // namespace hypershare::storage