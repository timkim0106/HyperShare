#include "hypershare/network/file_protocol.hpp"
#include <sstream>
#include <cstring>

namespace hypershare::network {

// Helper functions for serialization
namespace {
    void write_string(std::ostringstream& oss, const std::string& str) {
        uint32_t size = static_cast<uint32_t>(str.size());
        oss.write(reinterpret_cast<const char*>(&size), sizeof(size));
        oss.write(str.c_str(), size);
    }
    
    std::string read_string(const uint8_t*& data, size_t& remaining) {
        if (remaining < sizeof(uint32_t)) return "";
        
        uint32_t size;
        std::memcpy(&size, data, sizeof(size));
        data += sizeof(size);
        remaining -= sizeof(size);
        
        if (remaining < size) return "";
        
        std::string result(reinterpret_cast<const char*>(data), size);
        data += size;
        remaining -= size;
        
        return result;
    }
    
    template<typename T>
    void write_value(std::ostringstream& oss, const T& value) {
        oss.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    
    template<typename T>
    T read_value(const uint8_t*& data, size_t& remaining) {
        if (remaining < sizeof(T)) return T{};
        
        T value;
        std::memcpy(&value, data, sizeof(value));
        data += sizeof(value);
        remaining -= sizeof(value);
        
        return value;
    }
    
    void write_vector_strings(std::ostringstream& oss, const std::vector<std::string>& vec) {
        uint32_t count = static_cast<uint32_t>(vec.size());
        write_value(oss, count);
        for (const auto& str : vec) {
            write_string(oss, str);
        }
    }
    
    std::vector<std::string> read_vector_strings(const uint8_t*& data, size_t& remaining) {
        uint32_t count = read_value<uint32_t>(data, remaining);
        std::vector<std::string> result;
        result.reserve(count);
        
        for (uint32_t i = 0; i < count; ++i) {
            result.push_back(read_string(data, remaining));
        }
        
        return result;
    }
    
    void write_vector_bytes(std::ostringstream& oss, const std::vector<uint8_t>& vec) {
        uint32_t size = static_cast<uint32_t>(vec.size());
        write_value(oss, size);
        oss.write(reinterpret_cast<const char*>(vec.data()), size);
    }
    
    std::vector<uint8_t> read_vector_bytes(const uint8_t*& data, size_t& remaining) {
        uint32_t size = read_value<uint32_t>(data, remaining);
        if (remaining < size) return {};
        
        std::vector<uint8_t> result(data, data + size);
        data += size;
        remaining -= size;
        
        return result;
    }
}

// FileAnnounceMessage implementation
std::vector<uint8_t> FileAnnounceMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_string(oss, filename);
    write_value(oss, file_size);
    write_string(oss, file_hash);
    write_vector_strings(oss, tags);
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::FILE_ANNOUNCE,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

FileAnnounceMessage FileAnnounceMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    FileAnnounceMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.filename = read_string(ptr, remaining);
    msg.file_size = read_value<uint64_t>(ptr, remaining);
    msg.file_hash = read_string(ptr, remaining);
    msg.tags = read_vector_strings(ptr, remaining);
    
    return msg;
}

// FileRequestMessage implementation
std::vector<uint8_t> FileRequestMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_value(oss, start_offset);
    write_value(oss, length);
    write_value(oss, preferred_chunk_size);
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::FILE_REQUEST,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

FileRequestMessage FileRequestMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    FileRequestMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.start_offset = read_value<uint64_t>(ptr, remaining);
    msg.length = read_value<uint64_t>(ptr, remaining);
    msg.preferred_chunk_size = read_value<uint32_t>(ptr, remaining);
    
    return msg;
}

// FileResponseMessage implementation
std::vector<uint8_t> FileResponseMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_value(oss, accepted);
    write_string(oss, error_message);
    
    if (accepted) {
        // Serialize metadata
        auto metadata_serialized = metadata.serialize();
        write_vector_bytes(oss, metadata_serialized);
    } else {
        // Write empty metadata
        write_value<uint32_t>(oss, 0);
    }
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::FILE_RESPONSE,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

FileResponseMessage FileResponseMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    FileResponseMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.accepted = read_value<bool>(ptr, remaining);
    msg.error_message = read_string(ptr, remaining);
    
    if (msg.accepted) {
        auto metadata_bytes = read_vector_bytes(ptr, remaining);
        if (!metadata_bytes.empty()) {
            msg.metadata = hypershare::storage::FileMetadata::deserialize(metadata_bytes);
        }
    }
    
    return msg;
}

// ChunkRequestMessage implementation
std::vector<uint8_t> ChunkRequestMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_value(oss, chunk_index);
    write_value(oss, chunk_size);
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::CHUNK_REQUEST,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

ChunkRequestMessage ChunkRequestMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    ChunkRequestMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.chunk_index = read_value<uint64_t>(ptr, remaining);
    msg.chunk_size = read_value<uint32_t>(ptr, remaining);
    
    return msg;
}

// ChunkDataMessage implementation
std::vector<uint8_t> ChunkDataMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_value(oss, chunk_index);
    write_vector_bytes(oss, data);
    write_string(oss, chunk_hash);
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::CHUNK_DATA,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

ChunkDataMessage ChunkDataMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    ChunkDataMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.chunk_index = read_value<uint64_t>(ptr, remaining);
    msg.data = read_vector_bytes(ptr, remaining);
    msg.chunk_hash = read_string(ptr, remaining);
    
    return msg;
}

// ChunkAckMessage implementation
std::vector<uint8_t> ChunkAckMessage::serialize() const {
    std::ostringstream oss;
    
    write_string(oss, file_id);
    write_value(oss, chunk_index);
    write_value(oss, success);
    write_string(oss, error_message);
    
    std::string str = oss.str();
    return file_protocol_utils::add_message_header(
        FileProtocolMessageType::CHUNK_ACK,
        std::vector<uint8_t>(str.begin(), str.end())
    );
}

ChunkAckMessage ChunkAckMessage::deserialize(const std::vector<uint8_t>& data) {
    auto payload = file_protocol_utils::extract_payload(data);
    
    ChunkAckMessage msg;
    const uint8_t* ptr = payload.data();
    size_t remaining = payload.size();
    
    msg.file_id = read_string(ptr, remaining);
    msg.chunk_index = read_value<uint64_t>(ptr, remaining);
    msg.success = read_value<bool>(ptr, remaining);
    msg.error_message = read_string(ptr, remaining);
    
    return msg;
}

// Utility functions
namespace file_protocol_utils {
    FileProtocolMessageType get_message_type(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(uint8_t)) {
            return static_cast<FileProtocolMessageType>(0xFF); // Invalid
        }
        return static_cast<FileProtocolMessageType>(data[0]);
    }
    
    std::vector<uint8_t> add_message_header(FileProtocolMessageType type, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> result;
        result.reserve(1 + sizeof(uint32_t) + payload.size());
        
        // Add message type
        result.push_back(static_cast<uint8_t>(type));
        
        // Add payload size
        uint32_t size = static_cast<uint32_t>(payload.size());
        const uint8_t* size_bytes = reinterpret_cast<const uint8_t*>(&size);
        result.insert(result.end(), size_bytes, size_bytes + sizeof(size));
        
        // Add payload
        result.insert(result.end(), payload.begin(), payload.end());
        
        return result;
    }
    
    std::vector<uint8_t> extract_payload(const std::vector<uint8_t>& data) {
        if (data.size() < 1 + sizeof(uint32_t)) {
            return {};
        }
        
        // Skip message type (1 byte)
        const uint8_t* ptr = data.data() + 1;
        
        // Read payload size
        uint32_t payload_size;
        std::memcpy(&payload_size, ptr, sizeof(payload_size));
        ptr += sizeof(payload_size);
        
        // Validate payload size
        size_t expected_total_size = 1 + sizeof(uint32_t) + payload_size;
        if (data.size() != expected_total_size) {
            return {};
        }
        
        // Extract payload
        return std::vector<uint8_t>(ptr, ptr + payload_size);
    }
    
    bool validate_message_size(const std::vector<uint8_t>& data, size_t max_size) {
        return data.size() <= max_size;
    }
}

} // namespace hypershare::network