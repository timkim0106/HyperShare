#include "hypershare/storage/chunk_manager.hpp"
#include "hypershare/crypto/hash.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace hypershare::storage {

ChunkManager::ChunkManager(size_t chunk_size) : chunk_size_(chunk_size) {
}

ChunkManager::ChunkManager(const StorageConfig& config) 
    : chunk_size_(config.default_chunk_size), config_(config) {
}

std::vector<std::vector<uint8_t>> ChunkManager::split_file(const std::filesystem::path& file_path) {
    std::vector<std::vector<uint8_t>> chunks;
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return chunks;
    }
    
    std::vector<uint8_t> buffer(chunk_size_);
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), chunk_size_);
        std::streamsize bytes_read = file.gcount();
        
        if (bytes_read > 0) {
            std::vector<uint8_t> chunk(buffer.begin(), buffer.begin() + bytes_read);
            chunks.push_back(std::move(chunk));
        }
    }
    
    return chunks;
}

std::vector<std::string> ChunkManager::get_chunk_hashes(const std::filesystem::path& file_path) {
    std::vector<std::string> hashes;
    auto chunks = split_file(file_path);
    
    for (const auto& chunk : chunks) {
        hashes.push_back(compute_chunk_hash(chunk));
    }
    
    return hashes;
}

hypershare::crypto::CryptoResult ChunkManager::chunk_file(const std::string& file_path, FileMetadata& metadata) {
    std::filesystem::path path(file_path);
    
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_NOT_FOUND,
            "File not found: " + file_path
        );
    }
    
    try {
        // Get file size
        auto file_size = std::filesystem::file_size(path);
        
        // Get chunk hashes
        auto chunk_hashes = get_chunk_hashes(path);
        
        // Calculate file hash
        hypershare::crypto::Blake3Hash file_hash_raw;
        auto hash_result = hypershare::crypto::Blake3Hasher::hash_file(path, file_hash_raw);
        if (!hash_result) {
            return hypershare::crypto::CryptoResult(
                hypershare::crypto::CryptoError::FILE_READ_ERROR,
                "Failed to hash file: " + file_path
            );
        }
        
        // Fill metadata
        metadata.file_path = file_path;
        metadata.filename = path.filename().string();
        metadata.file_size = file_size;
        metadata.chunk_size = chunk_size_;
        metadata.chunk_count = static_cast<uint32_t>(chunk_hashes.size());
        metadata.chunk_hashes = std::move(chunk_hashes);
        metadata.file_hash = hypershare::crypto::hash_utils::hash_to_hex(file_hash_raw);
        metadata.created_at = std::chrono::system_clock::now();
        metadata.modified_at = std::chrono::system_clock::now();
        
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
        
    } catch (const std::exception& e) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_READ_ERROR,
            "Error processing file: " + std::string(e.what())
        );
    }
}

hypershare::crypto::CryptoResult ChunkManager::write_chunk(const FileMetadata& metadata,
                                                            size_t chunk_index,
                                                            const std::vector<uint8_t>& chunk_data) {
    if (!config_) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "ChunkManager not initialized with StorageConfig"
        );
    }
    
    // Use incomplete directory for storing chunks
    auto base_path = config_->get_incomplete_path(metadata.file_hash);
    
    bool success = write_chunk(base_path.parent_path(), metadata.file_hash, chunk_index, chunk_data);
    if (success) {
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    } else {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_WRITE_ERROR,
            "Failed to write chunk " + std::to_string(chunk_index)
        );
    }
}

hypershare::crypto::CryptoResult ChunkManager::read_chunk(const FileMetadata& metadata,
                                                           size_t chunk_index,
                                                           std::vector<uint8_t>& chunk_data) {
    if (!config_) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "ChunkManager not initialized with StorageConfig"
        );
    }
    
    // Try incomplete directory first
    auto incomplete_path = config_->get_incomplete_path(metadata.file_hash);
    chunk_data = read_chunk(incomplete_path.parent_path(), metadata.file_hash, chunk_index);
    
    if (!chunk_data.empty()) {
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    } else {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::FILE_READ_ERROR,
            "Failed to read chunk " + std::to_string(chunk_index)
        );
    }
}

bool ChunkManager::write_chunk(const std::filesystem::path& base_path,
                               const std::string& file_hash,
                               size_t chunk_index,
                               const std::vector<uint8_t>& chunk_data) {
    auto chunk_path = get_chunk_path(base_path, file_hash, chunk_index);
    
    // Create directory if it doesn't exist
    std::filesystem::create_directories(chunk_path.parent_path());
    
    std::ofstream file(chunk_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
    return file.good();
}

std::vector<uint8_t> ChunkManager::read_chunk(const std::filesystem::path& base_path,
                                              const std::string& file_hash,
                                              size_t chunk_index) {
    auto chunk_path = get_chunk_path(base_path, file_hash, chunk_index);
    
    std::ifstream file(chunk_path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    
    // Get file size
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> chunk_data(size);
    file.read(reinterpret_cast<char*>(chunk_data.data()), size);
    
    return chunk_data;
}

bool ChunkManager::merge_chunks(const std::filesystem::path& base_path,
                                const std::string& file_hash,
                                const std::filesystem::path& output_path,
                                size_t total_chunks) {
    std::ofstream output_file(output_path, std::ios::binary);
    if (!output_file.is_open()) {
        return false;
    }
    
    for (size_t i = 0; i < total_chunks; ++i) {
        auto chunk_data = read_chunk(base_path, file_hash, i);
        if (chunk_data.empty()) {
            return false;
        }
        
        output_file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
        if (!output_file.good()) {
            return false;
        }
    }
    
    return true;
}

bool ChunkManager::verify_chunk(const std::vector<uint8_t>& chunk_data,
                                const std::string& expected_hash) {
    return compute_chunk_hash(chunk_data) == expected_hash;
}

bool ChunkManager::verify_chunk_hash(const std::vector<uint8_t>& chunk_data,
                                     const std::string& expected_hash) {
    return verify_chunk(chunk_data, expected_hash);
}

std::filesystem::path ChunkManager::get_chunk_path(const std::filesystem::path& base_path,
                                                   const std::string& file_hash,
                                                   size_t chunk_index) {
    // Create subdirectory based on first two characters of hash for better file distribution
    std::string subdir = file_hash.substr(0, 2);
    
    std::ostringstream filename;
    filename << file_hash << ".chunk." << std::setfill('0') << std::setw(6) << chunk_index;
    
    return base_path / subdir / filename.str();
}

std::string ChunkManager::compute_chunk_hash(const std::vector<uint8_t>& chunk_data) {
    std::span<const uint8_t> data_span(chunk_data.data(), chunk_data.size());
    auto hash = hypershare::crypto::Blake3Hasher::hash(data_span);
    return hypershare::crypto::hash_utils::hash_to_hex(hash);
}

} // namespace hypershare::storage