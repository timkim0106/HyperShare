#include "hypershare/network/protocol.hpp"
#include <chrono>
#include <random>
#include <cstring>
#include <algorithm>

namespace hypershare::network {

namespace {
    std::uint64_t generate_message_id() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        return gen();
    }
    
    std::uint64_t get_timestamp_ns() {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
    
    std::uint32_t calculate_crc32(std::span<const std::uint8_t> data) {
        std::uint32_t crc = 0xFFFFFFFF;
        static constexpr std::uint32_t crc_table[256] = {
            0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
            0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
            // ... truncated for brevity, would include full CRC32 table
        };
        
        for (auto byte : data) {
            crc = crc_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFF;
    }
    
    void write_uint32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
        buffer.push_back((value >> 24) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_uint64(std::vector<std::uint8_t>& buffer, std::uint64_t value) {
        buffer.push_back((value >> 56) & 0xFF);
        buffer.push_back((value >> 48) & 0xFF);
        buffer.push_back((value >> 40) & 0xFF);
        buffer.push_back((value >> 32) & 0xFF);
        buffer.push_back((value >> 24) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_uint16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_string(std::vector<std::uint8_t>& buffer, const std::string& str) {
        write_uint32(buffer, static_cast<std::uint32_t>(str.size()));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }
    
    std::uint32_t read_uint32(std::span<const std::uint8_t>& data) {
        if (data.size() < 4) throw std::runtime_error("Insufficient data for uint32");
        std::uint32_t value = (static_cast<std::uint32_t>(data[0]) << 24) |
                             (static_cast<std::uint32_t>(data[1]) << 16) |
                             (static_cast<std::uint32_t>(data[2]) << 8) |
                             static_cast<std::uint32_t>(data[3]);
        data = data.subspan(4);
        return value;
    }
    
    std::uint64_t read_uint64(std::span<const std::uint8_t>& data) {
        if (data.size() < 8) throw std::runtime_error("Insufficient data for uint64");
        std::uint64_t value = (static_cast<std::uint64_t>(data[0]) << 56) |
                             (static_cast<std::uint64_t>(data[1]) << 48) |
                             (static_cast<std::uint64_t>(data[2]) << 40) |
                             (static_cast<std::uint64_t>(data[3]) << 32) |
                             (static_cast<std::uint64_t>(data[4]) << 24) |
                             (static_cast<std::uint64_t>(data[5]) << 16) |
                             (static_cast<std::uint64_t>(data[6]) << 8) |
                             static_cast<std::uint64_t>(data[7]);
        data = data.subspan(8);
        return value;
    }
    
    std::uint16_t read_uint16(std::span<const std::uint8_t>& data) {
        if (data.size() < 2) throw std::runtime_error("Insufficient data for uint16");
        std::uint16_t value = (static_cast<std::uint16_t>(data[0]) << 8) |
                             static_cast<std::uint16_t>(data[1]);
        data = data.subspan(2);
        return value;
    }
    
    std::string read_string(std::span<const std::uint8_t>& data) {
        auto length = read_uint32(data);
        if (data.size() < length) throw std::runtime_error("Insufficient data for string");
        std::string str(reinterpret_cast<const char*>(data.data()), length);
        data = data.subspan(length);
        return str;
    }
}

MessageHeader::MessageHeader() 
    : magic(PROTOCOL_MAGIC)
    , version(PROTOCOL_VERSION)
    , type(MessageType::HEARTBEAT)
    , flags(MessageFlags::NONE)
    , message_id(generate_message_id())
    , payload_size(0)
    , timestamp(get_timestamp_ns())
    , checksum{0, 0, 0, 0} {
}

MessageHeader::MessageHeader(MessageType msg_type, std::uint32_t payload_len)
    : magic(PROTOCOL_MAGIC)
    , version(PROTOCOL_VERSION)
    , type(msg_type)
    , flags(MessageFlags::NONE)
    , message_id(generate_message_id())
    , payload_size(payload_len)
    , timestamp(get_timestamp_ns())
    , checksum{0, 0, 0, 0} {
}

bool MessageHeader::is_valid() const {
    return magic == PROTOCOL_MAGIC && version == PROTOCOL_VERSION;
}

void MessageHeader::calculate_checksum(std::span<const std::uint8_t> payload) {
    auto crc = calculate_crc32(payload);
    checksum[0] = (crc >> 24) & 0xFF;
    checksum[1] = (crc >> 16) & 0xFF;
    checksum[2] = (crc >> 8) & 0xFF;
    checksum[3] = crc & 0xFF;
}

bool MessageHeader::verify_checksum(std::span<const std::uint8_t> payload) const {
    auto expected_crc = calculate_crc32(payload);
    auto actual_crc = (static_cast<std::uint32_t>(checksum[0]) << 24) |
                     (static_cast<std::uint32_t>(checksum[1]) << 16) |
                     (static_cast<std::uint32_t>(checksum[2]) << 8) |
                     static_cast<std::uint32_t>(checksum[3]);
    return expected_crc == actual_crc;
}

std::vector<std::uint8_t> MessageHeader::serialize() const {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(MESSAGE_HEADER_SIZE);
    
    write_uint32(buffer, magic);
    write_uint16(buffer, version);
    buffer.push_back(static_cast<std::uint8_t>(type));
    buffer.push_back(static_cast<std::uint8_t>(flags));
    write_uint64(buffer, message_id);
    write_uint32(buffer, payload_size);
    write_uint64(buffer, timestamp);
    buffer.insert(buffer.end(), checksum.begin(), checksum.end());
    
    return buffer;
}

MessageHeader MessageHeader::deserialize(std::span<const std::uint8_t> data) {
    if (data.size() < MESSAGE_HEADER_SIZE) {
        throw std::runtime_error("Insufficient data for message header");
    }
    
    MessageHeader header;
    auto span = data;
    
    header.magic = read_uint32(span);
    header.version = read_uint16(span);
    header.type = static_cast<MessageType>(span[0]);
    header.flags = static_cast<MessageFlags>(span[1]);
    span = span.subspan(2);
    header.message_id = read_uint64(span);
    header.payload_size = read_uint32(span);
    header.timestamp = read_uint64(span);
    std::copy(span.begin(), span.begin() + 4, header.checksum.begin());
    
    return header;
}

std::vector<std::uint8_t> HandshakeMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_uint16(buffer, listen_port);
    write_string(buffer, peer_name);
    write_uint32(buffer, capabilities);
    return buffer;
}

HandshakeMessage HandshakeMessage::deserialize(std::span<const std::uint8_t> data) {
    HandshakeMessage msg;
    auto span = data;
    msg.peer_id = read_uint32(span);
    msg.listen_port = read_uint16(span);
    msg.peer_name = read_string(span);
    msg.capabilities = read_uint32(span);
    return msg;
}

std::vector<std::uint8_t> HeartbeatMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint64(buffer, timestamp);
    write_uint32(buffer, active_connections);
    write_uint32(buffer, available_files);
    return buffer;
}

HeartbeatMessage HeartbeatMessage::deserialize(std::span<const std::uint8_t> data) {
    HeartbeatMessage msg;
    auto span = data;
    msg.timestamp = read_uint64(span);
    msg.active_connections = read_uint32(span);
    msg.available_files = read_uint32(span);
    return msg;
}

std::vector<std::uint8_t> PeerAnnounceMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_string(buffer, ip_address);
    write_uint16(buffer, port);
    write_uint64(buffer, last_seen);
    return buffer;
}

PeerAnnounceMessage PeerAnnounceMessage::deserialize(std::span<const std::uint8_t> data) {
    PeerAnnounceMessage msg;
    auto span = data;
    msg.peer_id = read_uint32(span);
    msg.ip_address = read_string(span);
    msg.port = read_uint16(span);
    msg.last_seen = read_uint64(span);
    return msg;
}

std::vector<std::uint8_t> FileAnnounceMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_string(buffer, file_id);
    write_string(buffer, filename);
    write_uint64(buffer, file_size);
    write_string(buffer, file_hash);
    write_uint32(buffer, static_cast<std::uint32_t>(tags.size()));
    for (const auto& tag : tags) {
        write_string(buffer, tag);
    }
    return buffer;
}

FileAnnounceMessage FileAnnounceMessage::deserialize(std::span<const std::uint8_t> data) {
    FileAnnounceMessage msg;
    auto span = data;
    msg.file_id = read_string(span);
    msg.filename = read_string(span);
    msg.file_size = read_uint64(span);
    msg.file_hash = read_string(span);
    auto tag_count = read_uint32(span);
    msg.tags.reserve(tag_count);
    for (std::uint32_t i = 0; i < tag_count; ++i) {
        msg.tags.push_back(read_string(span));
    }
    return msg;
}

std::vector<std::uint8_t> ChunkRequestMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_string(buffer, file_id);
    write_uint64(buffer, chunk_index);
    write_uint32(buffer, chunk_size);
    return buffer;
}

ChunkRequestMessage ChunkRequestMessage::deserialize(std::span<const std::uint8_t> data) {
    ChunkRequestMessage msg;
    auto span = data;
    msg.file_id = read_string(span);
    msg.chunk_index = read_uint64(span);
    msg.chunk_size = read_uint32(span);
    return msg;
}

std::vector<std::uint8_t> ChunkDataMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_string(buffer, file_id);
    write_uint64(buffer, chunk_index);
    write_uint32(buffer, static_cast<std::uint32_t>(data.size()));
    buffer.insert(buffer.end(), data.begin(), data.end());
    write_string(buffer, chunk_hash);
    return buffer;
}

ChunkDataMessage ChunkDataMessage::deserialize(std::span<const std::uint8_t> data_span) {
    ChunkDataMessage msg;
    auto span = data_span;
    msg.file_id = read_string(span);
    msg.chunk_index = read_uint64(span);
    auto data_size = read_uint32(span);
    if (span.size() < data_size) throw std::runtime_error("Insufficient data for chunk");
    msg.data.assign(span.begin(), span.begin() + data_size);
    span = span.subspan(data_size);
    msg.chunk_hash = read_string(span);
    return msg;
}

std::vector<std::uint8_t> ErrorMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, error_code);
    write_string(buffer, error_message);
    write_uint64(buffer, request_id);
    return buffer;
}

ErrorMessage ErrorMessage::deserialize(std::span<const std::uint8_t> data) {
    ErrorMessage msg;
    auto span = data;
    msg.error_code = read_uint32(span);
    msg.error_message = read_string(span);
    msg.request_id = read_uint64(span);
    return msg;
}

}