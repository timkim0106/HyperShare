#include "hypershare/crypto/file_verification.hpp"
#include "hypershare/crypto/hash.hpp"
#include <fstream>

namespace hypershare::crypto {

FileVerifier::FileVerifier() {
}

bool FileVerifier::verify_chunk(const std::vector<uint8_t>& chunk_data, const std::string& expected_hash) {
    auto calculated_hash = calculate_chunk_hash(chunk_data);
    return compare_hashes(calculated_hash, expected_hash);
}

bool FileVerifier::verify_chunk(const std::vector<uint8_t>& chunk_data, const Blake3Hash& expected_hash) {
    auto calculated_hash = calculate_chunk_hash_raw(chunk_data);
    return compare_hashes(calculated_hash, expected_hash);
}

std::string FileVerifier::calculate_chunk_hash(const std::vector<uint8_t>& chunk_data) {
    auto hash_raw = calculate_chunk_hash_raw(chunk_data);
    return hash_utils::hash_to_hex(hash_raw);
}

Blake3Hash FileVerifier::calculate_chunk_hash_raw(const std::vector<uint8_t>& chunk_data) {
    std::span<const uint8_t> data_span(chunk_data.data(), chunk_data.size());
    return Blake3Hasher::hash(data_span);
}

bool FileVerifier::verify_file(const std::filesystem::path& file_path, const std::string& expected_hash) {
    auto calculated_hash = calculate_file_hash(file_path);
    return compare_hashes(calculated_hash, expected_hash);
}

bool FileVerifier::verify_file(const std::filesystem::path& file_path, const Blake3Hash& expected_hash) {
    auto calculated_hash = calculate_file_hash_raw(file_path);
    return compare_hashes(calculated_hash, expected_hash);
}

std::string FileVerifier::calculate_file_hash(const std::filesystem::path& file_path) {
    auto hash_raw = calculate_file_hash_raw(file_path);
    return hash_utils::hash_to_hex(hash_raw);
}

Blake3Hash FileVerifier::calculate_file_hash_raw(const std::filesystem::path& file_path) {
    Blake3Hash result;
    Blake3Hasher::hash_file(file_path, result);
    return result;
}

bool FileVerifier::verify_file_metadata(const std::filesystem::path& file_path, 
                                        const hypershare::storage::FileMetadata& metadata) {
    // Check file size
    try {
        auto actual_size = std::filesystem::file_size(file_path);
        if (actual_size != metadata.file_size) {
            return false;
        }
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
    
    // Check file hash
    auto calculated_hash = calculate_file_hash(file_path);
    return compare_hashes(calculated_hash, metadata.file_hash);
}

bool FileVerifier::verify_all_chunks(const std::filesystem::path& file_path,
                                     const std::vector<std::string>& chunk_hashes,
                                     uint32_t chunk_size) {
    auto chunks = split_file_into_chunks(file_path, chunk_size);
    
    if (chunks.size() != chunk_hashes.size()) {
        return false;
    }
    
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!verify_chunk(chunks[i], chunk_hashes[i])) {
            return false;
        }
    }
    
    return true;
}

FileVerifier::CorruptionReport FileVerifier::check_file_integrity(const std::filesystem::path& file_path,
                                                                  const hypershare::storage::FileMetadata& metadata) {
    CorruptionReport report;
    report.is_corrupted = false;
    
    // Check if file exists
    if (!std::filesystem::exists(file_path)) {
        report.is_corrupted = true;
        report.details = "File does not exist";
        return report;
    }
    
    // Check file size
    try {
        auto actual_size = std::filesystem::file_size(file_path);
        if (actual_size != metadata.file_size) {
            report.is_corrupted = true;
            report.details = "File size mismatch";
            return report;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        report.is_corrupted = true;
        report.details = "Cannot read file size: " + std::string(e.what());
        return report;
    }
    
    // Check file hash
    auto calculated_hash = calculate_file_hash(file_path);
    if (!compare_hashes(calculated_hash, metadata.file_hash)) {
        report.is_corrupted = true;
        report.file_hash_mismatch = calculated_hash;
        report.details = "File hash mismatch";
    }
    
    return report;
}

bool FileVerifier::verify_file_with_progress(const std::filesystem::path& file_path,
                                             const hypershare::storage::FileMetadata& metadata,
                                             ProgressCallback callback) {
    // Implementation stub - for now just verify without progress
    return verify_file_metadata(file_path, metadata);
}

std::vector<std::vector<uint8_t>> FileVerifier::split_file_into_chunks(const std::filesystem::path& file_path,
                                                                       uint32_t chunk_size) {
    std::vector<std::vector<uint8_t>> chunks;
    
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return chunks;
    }
    
    std::vector<uint8_t> buffer(chunk_size);
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), chunk_size);
        std::streamsize bytes_read = file.gcount();
        
        if (bytes_read > 0) {
            std::vector<uint8_t> chunk(buffer.begin(), buffer.begin() + bytes_read);
            chunks.push_back(std::move(chunk));
        }
    }
    
    return chunks;
}

bool FileVerifier::compare_hashes(const std::string& hash1, const std::string& hash2) {
    return hash1 == hash2;
}

bool FileVerifier::compare_hashes(const Blake3Hash& hash1, const Blake3Hash& hash2) {
    return hash1 == hash2;
}

} // namespace hypershare::crypto