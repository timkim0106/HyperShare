#include "hypershare/storage/file_index.hpp"
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>

namespace hypershare::storage {

FileIndex::FileIndex(const std::filesystem::path& db_path) 
    : db_path_(db_path), db_(nullptr) {
}

FileIndex::~FileIndex() {
    cleanup_statements();
    if (db_) {
        sqlite3_close(db_);
    }
}

bool FileIndex::initialize() {
    int result = sqlite3_open(db_path_.string().c_str(), &db_);
    if (result != SQLITE_OK) {
        return false;
    }
    
    return create_tables() && prepare_statements();
}

bool FileIndex::create_tables() {
    const char* create_files_table = R"(
        CREATE TABLE IF NOT EXISTS files (
            file_hash TEXT PRIMARY KEY,
            filename TEXT NOT NULL,
            file_size INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            modified_at INTEGER NOT NULL,
            chunk_size INTEGER NOT NULL,
            file_type TEXT,
            description TEXT,
            metadata_blob BLOB
        );
    )";
    
    const char* create_chunks_table = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            file_hash TEXT NOT NULL,
            chunk_index INTEGER NOT NULL,
            chunk_hash TEXT NOT NULL,
            is_available INTEGER DEFAULT 0,
            PRIMARY KEY (file_hash, chunk_index),
            FOREIGN KEY (file_hash) REFERENCES files(file_hash) ON DELETE CASCADE
        );
    )";
    
    const char* create_indexes = R"(
        CREATE INDEX IF NOT EXISTS idx_files_filename ON files(filename);
        CREATE INDEX IF NOT EXISTS idx_files_created_at ON files(created_at);
        CREATE INDEX IF NOT EXISTS idx_chunks_hash ON chunks(chunk_hash);
        CREATE INDEX IF NOT EXISTS idx_chunks_available ON chunks(is_available);
    )";
    
    char* error_msg = nullptr;
    
    int result = sqlite3_exec(db_, create_files_table, nullptr, nullptr, &error_msg);
    if (result != SQLITE_OK) {
        sqlite3_free(error_msg);
        return false;
    }
    
    result = sqlite3_exec(db_, create_chunks_table, nullptr, nullptr, &error_msg);
    if (result != SQLITE_OK) {
        sqlite3_free(error_msg);
        return false;
    }
    
    result = sqlite3_exec(db_, create_indexes, nullptr, nullptr, &error_msg);
    if (result != SQLITE_OK) {
        sqlite3_free(error_msg);
        return false;
    }
    
    return true;
}

bool FileIndex::prepare_statements() {
    // For now, we'll use sqlite3_exec directly
    // In a production implementation, we'd prepare statements for better performance
    return true;
}

void FileIndex::cleanup_statements() {
    // Clean up prepared statements if any
}

bool FileIndex::add_file(const FileMetadata& metadata) {
    const char* insert_file_sql = R"(
        INSERT OR REPLACE INTO files 
        (file_hash, filename, file_size, created_at, modified_at, chunk_size, file_type, description, metadata_blob)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, insert_file_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return false;
    }
    
    auto created_time = metadata.created_at.time_since_epoch().count();
    auto modified_time = metadata.modified_at.time_since_epoch().count();
    auto serialized = serialize_metadata(metadata);
    
    sqlite3_bind_text(stmt, 1, metadata.file_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, metadata.filename.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, metadata.file_size);
    sqlite3_bind_int64(stmt, 4, created_time);
    sqlite3_bind_int64(stmt, 5, modified_time);
    sqlite3_bind_int(stmt, 6, metadata.chunk_size);
    sqlite3_bind_text(stmt, 7, metadata.file_type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, metadata.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 9, serialized.data(), serialized.size(), SQLITE_STATIC);
    
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (result != SQLITE_DONE) {
        return false;
    }
    
    // Insert chunk information
    for (size_t i = 0; i < metadata.chunk_hashes.size(); ++i) {
        const char* insert_chunk_sql = R"(
            INSERT OR REPLACE INTO chunks (file_hash, chunk_index, chunk_hash, is_available)
            VALUES (?, ?, ?, 1);
        )";
        
        sqlite3_stmt* chunk_stmt;
        result = sqlite3_prepare_v2(db_, insert_chunk_sql, -1, &chunk_stmt, nullptr);
        if (result != SQLITE_OK) {
            continue;
        }
        
        sqlite3_bind_text(chunk_stmt, 1, metadata.file_hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(chunk_stmt, 2, i);
        sqlite3_bind_text(chunk_stmt, 3, metadata.chunk_hashes[i].c_str(), -1, SQLITE_STATIC);
        
        sqlite3_step(chunk_stmt);
        sqlite3_finalize(chunk_stmt);
    }
    
    return true;
}

bool FileIndex::update_file(const FileMetadata& metadata) {
    return add_file(metadata); // Same as add with REPLACE
}

hypershare::crypto::CryptoResult FileIndex::remove_file(const std::string& file_hash) {
    const char* delete_sql = "DELETE FROM files WHERE file_hash = ?;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, delete_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_WRITE_ERROR,
            "Failed to prepare delete statement"
        );
    }
    
    sqlite3_bind_text(stmt, 1, file_hash.c_str(), -1, SQLITE_STATIC);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (result == SQLITE_DONE) {
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    } else {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_WRITE_ERROR,
            "Failed to remove file from database"
        );
    }
}

std::optional<FileMetadata> FileIndex::get_file(const std::string& file_hash) {
    const char* select_sql = "SELECT metadata_blob FROM files WHERE file_hash = ?;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, file_hash.c_str(), -1, SQLITE_STATIC);
    result = sqlite3_step(stmt);
    
    if (result != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    
    const void* blob_data = sqlite3_column_blob(stmt, 0);
    int blob_size = sqlite3_column_bytes(stmt, 0);
    
    std::vector<uint8_t> serialized_data(
        static_cast<const uint8_t*>(blob_data),
        static_cast<const uint8_t*>(blob_data) + blob_size
    );
    
    sqlite3_finalize(stmt);
    
    return deserialize_metadata(serialized_data);
}

hypershare::crypto::CryptoResult FileIndex::get_file(const std::string& file_id, FileMetadata& metadata) {
    // For now, assume file_id is the same as file_hash
    // In a real implementation, we'd have a separate file_id field
    auto result = get_file(file_id);
    if (result) {
        metadata = *result;
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    } else {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_NOT_FOUND,
            "File not found with ID: " + file_id
        );
    }
}

std::vector<FileMetadata> FileIndex::list_files() {
    std::vector<FileMetadata> files;
    const char* select_sql = "SELECT metadata_blob FROM files ORDER BY created_at DESC;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return files;
    }
    
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const void* blob_data = sqlite3_column_blob(stmt, 0);
        int blob_size = sqlite3_column_bytes(stmt, 0);
        
        std::vector<uint8_t> serialized_data(
            static_cast<const uint8_t*>(blob_data),
            static_cast<const uint8_t*>(blob_data) + blob_size
        );
        
        files.push_back(deserialize_metadata(serialized_data));
    }
    
    sqlite3_finalize(stmt);
    return files;
}

std::vector<FileMetadata> FileIndex::search_files(const std::string& query) {
    std::vector<FileMetadata> files;
    const char* search_sql = R"(
        SELECT metadata_blob FROM files 
        WHERE filename LIKE ? OR file_type LIKE ? OR description LIKE ?
        ORDER BY created_at DESC;
    )";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, search_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return files;
    }
    
    std::string search_pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, search_pattern.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, search_pattern.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, search_pattern.c_str(), -1, SQLITE_STATIC);
    
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        const void* blob_data = sqlite3_column_blob(stmt, 0);
        int blob_size = sqlite3_column_bytes(stmt, 0);
        
        std::vector<uint8_t> serialized_data(
            static_cast<const uint8_t*>(blob_data),
            static_cast<const uint8_t*>(blob_data) + blob_size
        );
        
        files.push_back(deserialize_metadata(serialized_data));
    }
    
    sqlite3_finalize(stmt);
    return files;
}

bool FileIndex::file_exists(const std::string& file_hash) {
    const char* exists_sql = "SELECT 1 FROM files WHERE file_hash = ? LIMIT 1;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, exists_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, file_hash.c_str(), -1, SQLITE_STATIC);
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return result == SQLITE_ROW;
}

size_t FileIndex::get_file_count() {
    const char* count_sql = "SELECT COUNT(*) FROM files;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return 0;
    }
    
    result = sqlite3_step(stmt);
    size_t count = (result == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    
    return count;
}

uint64_t FileIndex::get_total_size() {
    const char* size_sql = "SELECT SUM(file_size) FROM files;";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, size_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return 0;
    }
    
    result = sqlite3_step(stmt);
    uint64_t total_size = (result == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    
    return total_size;
}

bool FileIndex::update_chunk_progress(const std::string& file_hash,
                                      size_t chunk_index,
                                      const std::string& chunk_hash) {
    const char* update_sql = R"(
        INSERT OR REPLACE INTO chunks (file_hash, chunk_index, chunk_hash, is_available)
        VALUES (?, ?, ?, 1);
    )";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, file_hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, chunk_index);
    sqlite3_bind_text(stmt, 3, chunk_hash.c_str(), -1, SQLITE_STATIC);
    
    result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return result == SQLITE_DONE;
}

std::vector<size_t> FileIndex::get_missing_chunks(const std::string& file_hash) {
    std::vector<size_t> missing_chunks;
    
    // First get total expected chunks
    auto metadata_opt = get_file(file_hash);
    if (!metadata_opt) {
        return missing_chunks;
    }
    
    auto metadata = *metadata_opt;
    size_t total_chunks = metadata.total_chunks();
    
    // Get available chunks
    const char* available_sql = R"(
        SELECT chunk_index FROM chunks 
        WHERE file_hash = ? AND is_available = 1
        ORDER BY chunk_index;
    )";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, available_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return missing_chunks;
    }
    
    sqlite3_bind_text(stmt, 1, file_hash.c_str(), -1, SQLITE_STATIC);
    
    std::vector<bool> available(total_chunks, false);
    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        size_t chunk_index = sqlite3_column_int64(stmt, 0);
        if (chunk_index < total_chunks) {
            available[chunk_index] = true;
        }
    }
    sqlite3_finalize(stmt);
    
    // Find missing chunks
    for (size_t i = 0; i < total_chunks; ++i) {
        if (!available[i]) {
            missing_chunks.push_back(i);
        }
    }
    
    return missing_chunks;
}

void FileIndex::cleanup_incomplete_files(const std::chrono::system_clock::time_point& cutoff_time) {
    auto cutoff_timestamp = cutoff_time.time_since_epoch().count();
    
    const char* cleanup_sql = R"(
        DELETE FROM files 
        WHERE created_at < ? AND file_hash IN (
            SELECT f.file_hash FROM files f
            LEFT JOIN chunks c ON f.file_hash = c.file_hash AND c.is_available = 1
            GROUP BY f.file_hash
            HAVING COUNT(c.chunk_index) < (f.file_size + f.chunk_size - 1) / f.chunk_size
        );
    )";
    
    sqlite3_stmt* stmt;
    int result = sqlite3_prepare_v2(db_, cleanup_sql, -1, &stmt, nullptr);
    if (result != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_int64(stmt, 1, cutoff_timestamp);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool FileIndex::vacuum_database() {
    char* error_msg = nullptr;
    int result = sqlite3_exec(db_, "VACUUM;", nullptr, nullptr, &error_msg);
    
    if (error_msg) {
        sqlite3_free(error_msg);
    }
    
    return result == SQLITE_OK;
}

std::vector<uint8_t> FileIndex::serialize_metadata(const FileMetadata& metadata) {
    return metadata.serialize();
}

FileMetadata FileIndex::deserialize_metadata(const std::vector<uint8_t>& data) {
    return FileMetadata::deserialize(data);
}

} // namespace hypershare::storage