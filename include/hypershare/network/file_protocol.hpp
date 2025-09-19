#pragma once

#include "../storage/file_metadata.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace hypershare::network {

// Base class for all file protocol messages
struct FileProtocolMessage {
    virtual ~FileProtocolMessage() = default;
    virtual std::vector<uint8_t> serialize() const = 0;
};

// File announcement message - broadcast available files
struct FileAnnounceMessage : public FileProtocolMessage {
    std::string file_id;
    std::string filename;
    uint64_t file_size;
    std::string file_hash;
    std::vector<std::string> tags;
    
    std::vector<uint8_t> serialize() const override;
    static FileAnnounceMessage deserialize(const std::vector<uint8_t>& data);
};

// File request message - request a file or portion of a file
struct FileRequestMessage : public FileProtocolMessage {
    std::string file_id;
    uint64_t start_offset;
    uint64_t length;
    uint32_t preferred_chunk_size;
    
    std::vector<uint8_t> serialize() const override;
    static FileRequestMessage deserialize(const std::vector<uint8_t>& data);
};

// File response message - respond to file request
struct FileResponseMessage : public FileProtocolMessage {
    std::string file_id;
    bool accepted;
    std::string error_message;
    hypershare::storage::FileMetadata metadata; // Only set if accepted
    
    std::vector<uint8_t> serialize() const override;
    static FileResponseMessage deserialize(const std::vector<uint8_t>& data);
};

// Chunk request message - request specific chunks
struct ChunkRequestMessage : public FileProtocolMessage {
    std::string file_id;
    uint64_t chunk_index;
    uint32_t chunk_size;
    
    std::vector<uint8_t> serialize() const override;
    static ChunkRequestMessage deserialize(const std::vector<uint8_t>& data);
};

// Chunk data message - send chunk data
struct ChunkDataMessage : public FileProtocolMessage {
    std::string file_id;
    uint64_t chunk_index;
    std::vector<uint8_t> data;
    std::string chunk_hash;
    
    std::vector<uint8_t> serialize() const override;
    static ChunkDataMessage deserialize(const std::vector<uint8_t>& data);
};

// Chunk acknowledgment message - acknowledge chunk receipt
struct ChunkAckMessage : public FileProtocolMessage {
    std::string file_id;
    uint64_t chunk_index;
    bool success;
    std::string error_message;
    
    std::vector<uint8_t> serialize() const override;
    static ChunkAckMessage deserialize(const std::vector<uint8_t>& data);
};

// Message type enumeration for protocol identification
enum class FileProtocolMessageType : uint8_t {
    FILE_ANNOUNCE = 0x10,
    FILE_REQUEST = 0x11,
    FILE_RESPONSE = 0x12,
    CHUNK_REQUEST = 0x13,
    CHUNK_DATA = 0x14,
    CHUNK_ACK = 0x15
};

// Utility functions for message handling
namespace file_protocol_utils {
    FileProtocolMessageType get_message_type(const std::vector<uint8_t>& data);
    std::vector<uint8_t> add_message_header(FileProtocolMessageType type, const std::vector<uint8_t>& payload);
    std::vector<uint8_t> extract_payload(const std::vector<uint8_t>& data);
    bool validate_message_size(const std::vector<uint8_t>& data, size_t max_size = 10 * 1024 * 1024); // 10MB default
}

} // namespace hypershare::network