#include "hypershare/storage/file_metadata.hpp"
#include <sstream>
#include <iomanip>

namespace hypershare::storage {

FileMetadata::FileMetadata(const std::string& hash, const std::string& name, uint64_t size)
    : file_hash(hash)
    , filename(name)
    , file_size(size)
    , created_at(std::chrono::system_clock::now())
    , modified_at(std::chrono::system_clock::now())
    , chunk_size(65536) // 64KB default
    , chunk_count(0)
{
}

std::vector<uint8_t> FileMetadata::serialize() const {
    std::ostringstream oss;
    
    // Write file_id
    uint32_t id_size = static_cast<uint32_t>(file_id.size());
    oss.write(reinterpret_cast<const char*>(&id_size), sizeof(id_size));
    oss.write(file_id.c_str(), id_size);
    
    // Write file_hash
    uint32_t hash_size = static_cast<uint32_t>(file_hash.size());
    oss.write(reinterpret_cast<const char*>(&hash_size), sizeof(hash_size));
    oss.write(file_hash.c_str(), hash_size);
    
    // Write filename
    uint32_t name_size = static_cast<uint32_t>(filename.size());
    oss.write(reinterpret_cast<const char*>(&name_size), sizeof(name_size));
    oss.write(filename.c_str(), name_size);
    
    // Write file_path
    uint32_t path_size = static_cast<uint32_t>(file_path.size());
    oss.write(reinterpret_cast<const char*>(&path_size), sizeof(path_size));
    oss.write(file_path.c_str(), path_size);
    
    // Write file_size
    oss.write(reinterpret_cast<const char*>(&file_size), sizeof(file_size));
    
    // Write timestamps
    auto created_time = created_at.time_since_epoch().count();
    auto modified_time = modified_at.time_since_epoch().count();
    oss.write(reinterpret_cast<const char*>(&created_time), sizeof(created_time));
    oss.write(reinterpret_cast<const char*>(&modified_time), sizeof(modified_time));
    
    // Write chunk_hashes
    uint32_t chunk_count = static_cast<uint32_t>(chunk_hashes.size());
    oss.write(reinterpret_cast<const char*>(&chunk_count), sizeof(chunk_count));
    for (const auto& chunk_hash : chunk_hashes) {
        uint32_t chunk_hash_size = static_cast<uint32_t>(chunk_hash.size());
        oss.write(reinterpret_cast<const char*>(&chunk_hash_size), sizeof(chunk_hash_size));
        oss.write(chunk_hash.c_str(), chunk_hash_size);
    }
    
    // Write chunk_size
    oss.write(reinterpret_cast<const char*>(&chunk_size), sizeof(chunk_size));
    
    // Write chunk_count
    oss.write(reinterpret_cast<const char*>(&chunk_count), sizeof(chunk_count));
    
    // Write file_type
    uint32_t type_size = static_cast<uint32_t>(file_type.size());
    oss.write(reinterpret_cast<const char*>(&type_size), sizeof(type_size));
    oss.write(file_type.c_str(), type_size);
    
    // Write description
    uint32_t desc_size = static_cast<uint32_t>(description.size());
    oss.write(reinterpret_cast<const char*>(&desc_size), sizeof(desc_size));
    oss.write(description.c_str(), desc_size);
    
    std::string str = oss.str();
    return std::vector<uint8_t>(str.begin(), str.end());
}

FileMetadata FileMetadata::deserialize(const std::vector<uint8_t>& data) {
    FileMetadata metadata;
    size_t offset = 0;
    
    // Read file_id
    uint32_t id_size;
    std::memcpy(&id_size, data.data() + offset, sizeof(id_size));
    offset += sizeof(id_size);
    metadata.file_id = std::string(reinterpret_cast<const char*>(data.data() + offset), id_size);
    offset += id_size;
    
    // Read file_hash
    uint32_t hash_size;
    std::memcpy(&hash_size, data.data() + offset, sizeof(hash_size));
    offset += sizeof(hash_size);
    metadata.file_hash = std::string(reinterpret_cast<const char*>(data.data() + offset), hash_size);
    offset += hash_size;
    
    // Read filename
    uint32_t name_size;
    std::memcpy(&name_size, data.data() + offset, sizeof(name_size));
    offset += sizeof(name_size);
    metadata.filename = std::string(reinterpret_cast<const char*>(data.data() + offset), name_size);
    offset += name_size;
    
    // Read file_path
    uint32_t path_size;
    std::memcpy(&path_size, data.data() + offset, sizeof(path_size));
    offset += sizeof(path_size);
    metadata.file_path = std::string(reinterpret_cast<const char*>(data.data() + offset), path_size);
    offset += path_size;
    
    // Read file_size
    std::memcpy(&metadata.file_size, data.data() + offset, sizeof(metadata.file_size));
    offset += sizeof(metadata.file_size);
    
    // Read timestamps
    decltype(metadata.created_at.time_since_epoch().count()) created_time, modified_time;
    std::memcpy(&created_time, data.data() + offset, sizeof(created_time));
    offset += sizeof(created_time);
    std::memcpy(&modified_time, data.data() + offset, sizeof(modified_time));
    offset += sizeof(modified_time);
    
    metadata.created_at = std::chrono::system_clock::time_point(
        std::chrono::system_clock::duration(created_time));
    metadata.modified_at = std::chrono::system_clock::time_point(
        std::chrono::system_clock::duration(modified_time));
    
    // Read chunk_hashes
    uint32_t chunk_count;
    std::memcpy(&chunk_count, data.data() + offset, sizeof(chunk_count));
    offset += sizeof(chunk_count);
    
    metadata.chunk_hashes.reserve(chunk_count);
    for (uint32_t i = 0; i < chunk_count; ++i) {
        uint32_t chunk_hash_size;
        std::memcpy(&chunk_hash_size, data.data() + offset, sizeof(chunk_hash_size));
        offset += sizeof(chunk_hash_size);
        
        std::string chunk_hash(reinterpret_cast<const char*>(data.data() + offset), chunk_hash_size);
        metadata.chunk_hashes.push_back(chunk_hash);
        offset += chunk_hash_size;
    }
    
    // Read chunk_size
    std::memcpy(&metadata.chunk_size, data.data() + offset, sizeof(metadata.chunk_size));
    offset += sizeof(metadata.chunk_size);
    
    // Read chunk_count
    std::memcpy(&metadata.chunk_count, data.data() + offset, sizeof(metadata.chunk_count));
    offset += sizeof(metadata.chunk_count);
    
    // Read file_type
    uint32_t type_size;
    std::memcpy(&type_size, data.data() + offset, sizeof(type_size));
    offset += sizeof(type_size);
    metadata.file_type = std::string(reinterpret_cast<const char*>(data.data() + offset), type_size);
    offset += type_size;
    
    // Read description
    uint32_t desc_size;
    std::memcpy(&desc_size, data.data() + offset, sizeof(desc_size));
    offset += sizeof(desc_size);
    metadata.description = std::string(reinterpret_cast<const char*>(data.data() + offset), desc_size);
    
    return metadata;
}

void FileMetadata::add_chunk_hash(const std::string& hash) {
    chunk_hashes.push_back(hash);
}

bool FileMetadata::is_complete() const {
    if (chunk_hashes.empty()) return false;
    
    size_t expected_chunks = (file_size + chunk_size - 1) / chunk_size;
    return chunk_hashes.size() == expected_chunks;
}

double FileMetadata::progress() const {
    if (file_size == 0) return 1.0;
    
    size_t expected_chunks = (file_size + chunk_size - 1) / chunk_size;
    if (expected_chunks == 0) return 1.0;
    
    return static_cast<double>(chunk_hashes.size()) / expected_chunks;
}

size_t FileMetadata::total_chunks() const {
    return (file_size + chunk_size - 1) / chunk_size;
}

uint32_t FileMetadata::get_chunk_count() const {
    return chunk_count;
}

uint32_t FileMetadata::get_chunk_size(size_t chunk_index) const {
    // For most chunks, return the standard chunk size
    if (chunk_index < chunk_count - 1) {
        return chunk_size;
    }
    
    // For the last chunk, calculate the remaining bytes
    if (chunk_index == chunk_count - 1) {
        uint32_t last_chunk_size = file_size % chunk_size;
        return last_chunk_size == 0 ? chunk_size : last_chunk_size;
    }
    
    return 0; // Invalid chunk index
}

bool FileMetadata::operator==(const FileMetadata& other) const {
    return file_id == other.file_id &&
           file_hash == other.file_hash &&
           filename == other.filename &&
           file_path == other.file_path &&
           file_size == other.file_size &&
           chunk_hashes == other.chunk_hashes &&
           chunk_size == other.chunk_size &&
           chunk_count == other.chunk_count &&
           file_type == other.file_type &&
           description == other.description;
}

bool FileMetadata::operator!=(const FileMetadata& other) const {
    return !(*this == other);
}

} // namespace hypershare::storage