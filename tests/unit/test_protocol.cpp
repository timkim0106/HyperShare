#include <gtest/gtest.h>
#include "hypershare/network/protocol.hpp"

using namespace hypershare::network;

class ProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ProtocolTest, MessageHeaderConstruction) {
    MessageHeader header;
    
    EXPECT_EQ(header.magic, PROTOCOL_MAGIC);
    EXPECT_EQ(header.version, PROTOCOL_VERSION);
    EXPECT_TRUE(header.is_valid());
    EXPECT_GT(header.message_id, 0);
    EXPECT_GT(header.timestamp, 0);
}

TEST_F(ProtocolTest, MessageHeaderWithPayload) {
    MessageHeader header(MessageType::HANDSHAKE, 100);
    
    EXPECT_EQ(header.type, MessageType::HANDSHAKE);
    EXPECT_EQ(header.payload_size, 100);
    EXPECT_TRUE(header.is_valid());
}

TEST_F(ProtocolTest, MessageHeaderSerialization) {
    MessageHeader original(MessageType::HEARTBEAT, 50);
    original.flags = MessageFlags::COMPRESSED;
    
    auto serialized = original.serialize();
    EXPECT_EQ(serialized.size(), MESSAGE_HEADER_SIZE);
    
    auto deserialized = MessageHeader::deserialize(serialized);
    
    EXPECT_EQ(deserialized.magic, original.magic);
    EXPECT_EQ(deserialized.version, original.version);
    EXPECT_EQ(deserialized.type, original.type);
    EXPECT_EQ(deserialized.flags, original.flags);
    EXPECT_EQ(deserialized.message_id, original.message_id);
    EXPECT_EQ(deserialized.payload_size, original.payload_size);
    EXPECT_EQ(deserialized.timestamp, original.timestamp);
}

TEST_F(ProtocolTest, ChecksumCalculation) {
    std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5};
    MessageHeader header(MessageType::FILE_ANNOUNCE, static_cast<std::uint32_t>(payload.size()));
    
    header.calculate_checksum(payload);
    EXPECT_TRUE(header.verify_checksum(payload));
    
    // Modify payload and verify checksum fails
    payload[0] = 99;
    EXPECT_FALSE(header.verify_checksum(payload));
}

TEST_F(ProtocolTest, HandshakeMessageSerialization) {
    HandshakeMessage original{
        12345,
        8080,
        "TestPeer",
        0x12345678
    };
    
    auto serialized = original.serialize();
    auto deserialized = HandshakeMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.peer_id, original.peer_id);
    EXPECT_EQ(deserialized.listen_port, original.listen_port);
    EXPECT_EQ(deserialized.peer_name, original.peer_name);
    EXPECT_EQ(deserialized.capabilities, original.capabilities);
}

TEST_F(ProtocolTest, HeartbeatMessageSerialization) {
    HeartbeatMessage original{
        1234567890123456789ULL,
        5,
        100
    };
    
    auto serialized = original.serialize();
    auto deserialized = HeartbeatMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.timestamp, original.timestamp);
    EXPECT_EQ(deserialized.active_connections, original.active_connections);
    EXPECT_EQ(deserialized.available_files, original.available_files);
}

TEST_F(ProtocolTest, PeerAnnounceMessageSerialization) {
    PeerAnnounceMessage original{
        54321,
        "192.168.1.100",
        8080,
        9876543210ULL
    };
    
    auto serialized = original.serialize();
    auto deserialized = PeerAnnounceMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.peer_id, original.peer_id);
    EXPECT_EQ(deserialized.ip_address, original.ip_address);
    EXPECT_EQ(deserialized.port, original.port);
    EXPECT_EQ(deserialized.last_seen, original.last_seen);
}

TEST_F(ProtocolTest, FileAnnounceMessageSerialization) {
    FileAnnounceMessage original{
        "file123",
        "document.pdf",
        1048576,
        "sha256:abcdef123456",
        {"document", "pdf", "important"}
    };
    
    auto serialized = original.serialize();
    auto deserialized = FileAnnounceMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.file_id, original.file_id);
    EXPECT_EQ(deserialized.filename, original.filename);
    EXPECT_EQ(deserialized.file_size, original.file_size);
    EXPECT_EQ(deserialized.file_hash, original.file_hash);
    EXPECT_EQ(deserialized.tags, original.tags);
}

TEST_F(ProtocolTest, ChunkDataMessageSerialization) {
    std::vector<std::uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    ChunkDataMessage original{
        "file456",
        42,
        test_data,
        "chunk_hash_123"
    };
    
    auto serialized = original.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.file_id, original.file_id);
    EXPECT_EQ(deserialized.chunk_index, original.chunk_index);
    EXPECT_EQ(deserialized.data, original.data);
    EXPECT_EQ(deserialized.chunk_hash, original.chunk_hash);
}

TEST_F(ProtocolTest, ErrorMessageSerialization) {
    ErrorMessage original{
        static_cast<std::uint32_t>(ErrorCode::FILE_NOT_FOUND),
        "The requested file was not found",
        9876543210ULL
    };
    
    auto serialized = original.serialize();
    auto deserialized = ErrorMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.error_code, original.error_code);
    EXPECT_EQ(deserialized.error_message, original.error_message);
    EXPECT_EQ(deserialized.request_id, original.request_id);
}

TEST_F(ProtocolTest, EmptyStringHandling) {
    HandshakeMessage original{
        12345,
        8080,
        "",  // Empty peer name
        0
    };
    
    auto serialized = original.serialize();
    auto deserialized = HandshakeMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.peer_name, "");
}

TEST_F(ProtocolTest, LargeDataHandling) {
    std::vector<std::uint8_t> large_data(10000, 0xAB);
    
    ChunkDataMessage original{
        "large_file",
        999,
        large_data,
        "large_chunk_hash"
    };
    
    auto serialized = original.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.data.size(), large_data.size());
    EXPECT_EQ(deserialized.data, large_data);
}