#pragma once

#include "crypto_types.hpp"
#include "../storage/file_metadata.hpp"
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <cstdint>

namespace hypershare::crypto {

class FileVerifier {
public:
    FileVerifier();
    
    // Chunk verification
    bool verify_chunk(const std::vector<uint8_t>& chunk_data, const std::string& expected_hash);
    bool verify_chunk(const std::vector<uint8_t>& chunk_data, const Blake3Hash& expected_hash);
    
    std::string calculate_chunk_hash(const std::vector<uint8_t>& chunk_data);
    Blake3Hash calculate_chunk_hash_raw(const std::vector<uint8_t>& chunk_data);
    
    // File verification
    bool verify_file(const std::filesystem::path& file_path, const std::string& expected_hash);
    bool verify_file(const std::filesystem::path& file_path, const Blake3Hash& expected_hash);
    
    std::string calculate_file_hash(const std::filesystem::path& file_path);
    Blake3Hash calculate_file_hash_raw(const std::filesystem::path& file_path);
    
    // Metadata verification
    bool verify_file_metadata(const std::filesystem::path& file_path, 
                              const hypershare::storage::FileMetadata& metadata);
    
    // Multi-chunk verification
    bool verify_all_chunks(const std::filesystem::path& file_path,
                           const std::vector<std::string>& chunk_hashes,
                           uint32_t chunk_size);
    
    // Corruption detection
    struct CorruptionReport {
        bool is_corrupted;
        std::vector<uint64_t> corrupted_chunks;
        std::string file_hash_mismatch;
        std::string details;
    };
    
    CorruptionReport check_file_integrity(const std::filesystem::path& file_path,
                                          const hypershare::storage::FileMetadata& metadata);
    
    // Performance verification for large files
    struct VerificationProgress {
        uint64_t chunks_verified;
        uint64_t total_chunks;
        double percentage_complete;
        std::chrono::milliseconds elapsed_time;
        std::chrono::milliseconds estimated_remaining;
    };
    
    using ProgressCallback = std::function<void(const VerificationProgress&)>;
    
    bool verify_file_with_progress(const std::filesystem::path& file_path,
                                   const hypershare::storage::FileMetadata& metadata,
                                   ProgressCallback callback = nullptr);
    
private:
    // Internal helpers
    std::vector<std::vector<uint8_t>> split_file_into_chunks(const std::filesystem::path& file_path,
                                                             uint32_t chunk_size);
    
    bool compare_hashes(const std::string& hash1, const std::string& hash2);
    bool compare_hashes(const Blake3Hash& hash1, const Blake3Hash& hash2);
};

} // namespace hypershare::crypto