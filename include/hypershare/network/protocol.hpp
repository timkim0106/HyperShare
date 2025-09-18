#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <span>
#include <concepts>

namespace hypershare::network {

constexpr std::uint32_t PROTOCOL_MAGIC = 0x48595045; // "HYPE"
constexpr std::uint16_t PROTOCOL_VERSION = 1;
constexpr std::size_t MESSAGE_HEADER_SIZE = 32;

enum class MessageType : std::uint8_t {
    HANDSHAKE       = 0x01,
    HANDSHAKE_ACK   = 0x02,
    SECURE_HANDSHAKE = 0x05,
    SECURE_HANDSHAKE_ACK = 0x06,
    HEARTBEAT       = 0x03,
    DISCONNECT      = 0x04,

    PEER_ANNOUNCE   = 0x10,
    PEER_QUERY      = 0x11,
    PEER_RESPONSE   = 0x12,

    FILE_ANNOUNCE   = 0x20,
    FILE_REQUEST    = 0x21,
    FILE_RESPONSE   = 0x22,
    CHUNK_REQUEST   = 0x23,
    CHUNK_DATA      = 0x24,
    CHUNK_ACK       = 0x25,

    ROUTE_UPDATE    = 0x30,
    TOPOLOGY_SYNC   = 0x31,

    ERROR_RESPONSE  = 0xFF
};

enum class MessageFlags : std::uint8_t {
    NONE            = 0x00,
    COMPRESSED      = 0x01,
    ENCRYPTED       = 0x02,
    FRAGMENTED      = 0x04,
    PRIORITY        = 0x08
};

struct MessageHeader {
    std::uint32_t magic;           // Protocol magic number
    std::uint16_t version;         // Protocol version
    MessageType type;              // Message type
    MessageFlags flags;            // Message flags
    std::uint64_t message_id;      // Unique message ID
    std::uint32_t payload_size;    // Payload length in bytes
    std::uint64_t timestamp;       // Unix timestamp (nanoseconds)
    std::array<std::uint8_t, 4> checksum; // CRC32 of payload
    
    MessageHeader();
    MessageHeader(MessageType msg_type, std::uint32_t payload_len);
    
    bool is_valid() const;
    void calculate_checksum(std::span<const std::uint8_t> payload);
    bool verify_checksum(std::span<const std::uint8_t> payload) const;
    
    std::vector<std::uint8_t> serialize() const;
    static MessageHeader deserialize(std::span<const std::uint8_t> data);
} __attribute__((packed));

static_assert(sizeof(MessageHeader) == MESSAGE_HEADER_SIZE);

template<typename T>
concept MessagePayload = requires(T t) {
    { t.serialize() } -> std::convertible_to<std::vector<std::uint8_t>>;
    { T::deserialize(std::declval<std::span<const std::uint8_t>>()) } -> std::same_as<T>;
};

struct HandshakeMessage {
    std::uint32_t peer_id;
    std::uint16_t listen_port;
    std::string peer_name;
    std::uint32_t capabilities;
    
    std::vector<std::uint8_t> serialize() const;
    static HandshakeMessage deserialize(std::span<const std::uint8_t> data);
};

struct HeartbeatMessage {
    std::uint64_t timestamp;
    std::uint32_t active_connections;
    std::uint32_t available_files;
    
    std::vector<std::uint8_t> serialize() const;
    static HeartbeatMessage deserialize(std::span<const std::uint8_t> data);
};

struct PeerAnnounceMessage {
    std::uint32_t peer_id;
    std::string ip_address;
    std::uint16_t port;
    std::uint64_t last_seen;
    
    std::vector<std::uint8_t> serialize() const;
    static PeerAnnounceMessage deserialize(std::span<const std::uint8_t> data);
};

struct FileAnnounceMessage {
    std::string file_id;
    std::string filename;
    std::uint64_t file_size;
    std::string file_hash;
    std::vector<std::string> tags;
    
    std::vector<std::uint8_t> serialize() const;
    static FileAnnounceMessage deserialize(std::span<const std::uint8_t> data);
};

struct ChunkRequestMessage {
    std::string file_id;
    std::uint64_t chunk_index;
    std::uint32_t chunk_size;
    
    std::vector<std::uint8_t> serialize() const;
    static ChunkRequestMessage deserialize(std::span<const std::uint8_t> data);
};

struct ChunkDataMessage {
    std::string file_id;
    std::uint64_t chunk_index;
    std::vector<std::uint8_t> data;
    std::string chunk_hash;
    
    std::vector<std::uint8_t> serialize() const;
    static ChunkDataMessage deserialize(std::span<const std::uint8_t> data);
};

struct ErrorMessage {
    std::uint32_t error_code;
    std::string error_message;
    std::uint64_t request_id;
    
    std::vector<std::uint8_t> serialize() const;
    static ErrorMessage deserialize(std::span<const std::uint8_t> data);
};

enum class ErrorCode : std::uint32_t {
    NONE                    = 0,
    PROTOCOL_VERSION        = 1,
    INVALID_MESSAGE         = 2,
    AUTHENTICATION_FAILED   = 3,
    FILE_NOT_FOUND         = 4,
    CHUNK_NOT_AVAILABLE    = 5,
    TRANSFER_FAILED        = 6,
    PEER_UNAVAILABLE       = 7,
    RATE_LIMITED           = 8,
    INTERNAL_ERROR         = 99
};

}

static_assert(hypershare::network::MessagePayload<hypershare::network::HandshakeMessage>);
static_assert(hypershare::network::MessagePayload<hypershare::network::HeartbeatMessage>);