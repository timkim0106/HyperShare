#pragma once

#include "file_metadata.hpp"
#include "../crypto/crypto_types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <memory>

struct sqlite3;

namespace hypershare::storage {

class FileIndex {
public:
    explicit FileIndex(const std::filesystem::path& db_path);
    ~FileIndex();
    
    bool initialize();
    
    bool add_file(const FileMetadata& metadata);
    
    bool update_file(const FileMetadata& metadata);
    
    hypershare::crypto::CryptoResult remove_file(const std::string& file_hash);
    
    std::optional<FileMetadata> get_file(const std::string& file_hash);
    
    hypershare::crypto::CryptoResult get_file(const std::string& file_id, FileMetadata& metadata);
    
    std::vector<FileMetadata> list_files();
    
    std::vector<FileMetadata> search_files(const std::string& query);
    
    bool file_exists(const std::string& file_hash);
    
    size_t get_file_count();
    
    uint64_t get_total_size();
    
    bool update_chunk_progress(const std::string& file_hash, 
                               size_t chunk_index, 
                               const std::string& chunk_hash);
    
    std::vector<size_t> get_missing_chunks(const std::string& file_hash);
    
    void cleanup_incomplete_files(const std::chrono::system_clock::time_point& cutoff_time);
    
    bool vacuum_database();

private:
    std::filesystem::path db_path_;
    sqlite3* db_;
    
    bool create_tables();
    bool prepare_statements();
    void cleanup_statements();
    
    std::vector<uint8_t> serialize_metadata(const FileMetadata& metadata);
    FileMetadata deserialize_metadata(const std::vector<uint8_t>& data);
};

} // namespace hypershare::storage