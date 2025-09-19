#include <gtest/gtest.h>
#include "hypershare/network/protocol.hpp"
#include "hypershare/network/file_protocol.hpp"
#include "hypershare/storage/file_metadata.hpp"
#include <vector>
#include <string>

using namespace hypershare::network;
using namespace hypershare::storage;

class FileProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test file metadata
        test_metadata_.file_id = "test_file_789";
        test_metadata_.filename = "protocol_test.txt";
        test_metadata_.file_size = 65536 * 3; // 3 chunks
        test_metadata_.chunk_size = 65536;    // 64KB chunks  
        test_metadata_.chunk_count = 3;
        test_metadata_.tags = {"test", "protocol"};
        
        // Fill chunk hashes
        test_metadata_.chunk_hashes.resize(3);
        for (auto& hash : test_metadata_.chunk_hashes) {
            std::fill(hash.begin(), hash.end(), 0x42);
        }
    }
    
    FileMetadata test_metadata_;
};

// Test FileAnnounceMessage
TEST_F(FileProtocolTest, FileAnnounceMessage_Serialization) {
    FileAnnounceMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.filename = test_metadata_.filename;
    msg.file_size = test_metadata_.file_size;
    msg.file_hash = "blake3_hash_placeholder";
    msg.tags = test_metadata_.tags;
    
    // Test serialization/deserialization
    auto serialized = msg.serialize();
    EXPECT_FALSE(serialized.empty());
    
    auto deserialized = FileAnnounceMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.filename, deserialized.filename);
    EXPECT_EQ(msg.file_size, deserialized.file_size);
    EXPECT_EQ(msg.file_hash, deserialized.file_hash);
    EXPECT_EQ(msg.tags, deserialized.tags);
}

TEST_F(FileProtocolTest, FileAnnounceMessage_EmptyTags) {
    FileAnnounceMessage msg;
    msg.file_id = "test";
    msg.filename = "empty_tags.txt";
    msg.file_size = 1024;
    msg.file_hash = "hash";
    msg.tags = {}; // Empty tags
    
    auto serialized = msg.serialize();
    auto deserialized = FileAnnounceMessage::deserialize(serialized);
    
    EXPECT_TRUE(deserialized.tags.empty());
}

TEST_F(FileProtocolTest, FileAnnounceMessage_LargeTags) {
    FileAnnounceMessage msg;
    msg.file_id = "test";
    msg.filename = "many_tags.txt";
    msg.file_size = 1024;
    msg.file_hash = "hash";
    
    // Create many tags
    for (int i = 0; i < 100; ++i) {
        msg.tags.push_back("tag_" + std::to_string(i));
    }
    
    auto serialized = msg.serialize();
    auto deserialized = FileAnnounceMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.tags.size(), deserialized.tags.size());
    EXPECT_EQ(msg.tags, deserialized.tags);
}

// Test FileRequestMessage
TEST_F(FileProtocolTest, FileRequestMessage_Serialization) {
    FileRequestMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.start_offset = 65536; // Start from second chunk
    msg.length = 131072;      // Request 2 chunks
    msg.preferred_chunk_size = 32768;
    
    auto serialized = msg.serialize();
    auto deserialized = FileRequestMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.start_offset, deserialized.start_offset);
    EXPECT_EQ(msg.length, deserialized.length);
    EXPECT_EQ(msg.preferred_chunk_size, deserialized.preferred_chunk_size);
}

TEST_F(FileProtocolTest, FileRequestMessage_FullFileRequest) {
    FileRequestMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.start_offset = 0;
    msg.length = test_metadata_.file_size;
    msg.preferred_chunk_size = test_metadata_.chunk_size;
    
    auto serialized = msg.serialize();
    auto deserialized = FileRequestMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.start_offset, 0);
    EXPECT_EQ(msg.length, test_metadata_.file_size);
}

// Test FileResponseMessage
TEST_F(FileProtocolTest, FileResponseMessage_AcceptedResponse) {
    FileResponseMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.accepted = true;
    msg.error_message = "";
    msg.metadata = test_metadata_;
    
    auto serialized = msg.serialize();
    auto deserialized = FileResponseMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_TRUE(deserialized.accepted);
    EXPECT_TRUE(deserialized.error_message.empty());
    EXPECT_EQ(msg.metadata.file_id, deserialized.metadata.file_id);
    EXPECT_EQ(msg.metadata.file_size, deserialized.metadata.file_size);
}

TEST_F(FileProtocolTest, FileResponseMessage_RejectedResponse) {
    FileResponseMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.accepted = false;
    msg.error_message = "File not found";
    // metadata should be empty for rejected requests
    
    auto serialized = msg.serialize();
    auto deserialized = FileResponseMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_FALSE(deserialized.accepted);
    EXPECT_EQ(msg.error_message, deserialized.error_message);
}

// Test ChunkRequestMessage
TEST_F(FileProtocolTest, ChunkRequestMessage_Serialization) {
    ChunkRequestMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 5;
    msg.chunk_size = test_metadata_.chunk_size;
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkRequestMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.chunk_index, deserialized.chunk_index);
    EXPECT_EQ(msg.chunk_size, deserialized.chunk_size);
}

TEST_F(FileProtocolTest, ChunkRequestMessage_LargeChunkIndex) {
    ChunkRequestMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 0xFFFFFFFFFFFFFFFF; // Max uint64_t
    msg.chunk_size = test_metadata_.chunk_size;
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkRequestMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.chunk_index, deserialized.chunk_index);
}

// Test ChunkDataMessage
TEST_F(FileProtocolTest, ChunkDataMessage_Serialization) {
    ChunkDataMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 2;
    msg.data = std::vector<uint8_t>(test_metadata_.chunk_size, 0x42);
    msg.chunk_hash = "chunk_hash_placeholder";
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.chunk_index, deserialized.chunk_index);
    EXPECT_EQ(msg.data, deserialized.data);
    EXPECT_EQ(msg.chunk_hash, deserialized.chunk_hash);
}

TEST_F(FileProtocolTest, ChunkDataMessage_EmptyChunk) {
    ChunkDataMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 0;
    msg.data = {}; // Empty data
    msg.chunk_hash = "empty_hash";
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_TRUE(deserialized.data.empty());
    EXPECT_EQ(msg.chunk_hash, deserialized.chunk_hash);
}

TEST_F(FileProtocolTest, ChunkDataMessage_LargeChunk) {
    ChunkDataMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 0;
    msg.data = std::vector<uint8_t>(1024 * 1024, 0xAB); // 1MB chunk
    msg.chunk_hash = "large_chunk_hash";
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.data.size(), deserialized.data.size());
    EXPECT_EQ(msg.data, deserialized.data);
}

// Test ChunkAckMessage
TEST_F(FileProtocolTest, ChunkAckMessage_SuccessAck) {
    ChunkAckMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 3;
    msg.success = true;
    msg.error_message = "";
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkAckMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.chunk_index, deserialized.chunk_index);
    EXPECT_TRUE(deserialized.success);
    EXPECT_TRUE(deserialized.error_message.empty());
}

TEST_F(FileProtocolTest, ChunkAckMessage_ErrorAck) {
    ChunkAckMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 7;
    msg.success = false;
    msg.error_message = "Chunk verification failed";
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkAckMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.file_id, deserialized.file_id);
    EXPECT_EQ(msg.chunk_index, deserialized.chunk_index);
    EXPECT_FALSE(deserialized.success);
    EXPECT_EQ(msg.error_message, deserialized.error_message);
}

// Test message size limits and edge cases
TEST_F(FileProtocolTest, MessageSizes_WithinLimits) {
    // Test that messages don't exceed reasonable size limits
    
    // Large file announce
    FileAnnounceMessage large_announce;
    large_announce.file_id = std::string(256, 'x'); // Large file ID
    large_announce.filename = std::string(1024, 'y'); // Large filename
    large_announce.file_size = 0xFFFFFFFFFFFFFFFF; // Max file size
    large_announce.file_hash = std::string(128, 'z'); // Large hash
    
    for (int i = 0; i < 1000; ++i) {
        large_announce.tags.push_back("tag_" + std::to_string(i));
    }
    
    auto serialized = large_announce.serialize();
    EXPECT_LT(serialized.size(), 1024 * 1024); // Should be less than 1MB
    
    auto deserialized = FileAnnounceMessage::deserialize(serialized);
    EXPECT_EQ(large_announce.file_id, deserialized.file_id);
    EXPECT_EQ(large_announce.tags.size(), deserialized.tags.size());
}

TEST_F(FileProtocolTest, MessageSizes_ChunkDataLimits) {
    ChunkDataMessage msg;
    msg.file_id = "test";
    msg.chunk_index = 0;
    msg.chunk_hash = "hash";
    
    // Test various chunk sizes
    std::vector<size_t> test_sizes = {
        0,           // Empty
        1024,        // 1KB
        65536,       // 64KB (typical)
        1024 * 1024, // 1MB
        4 * 1024 * 1024 // 4MB (large)
    };
    
    for (size_t size : test_sizes) {
        msg.data = std::vector<uint8_t>(size, 0x55);
        
        auto serialized = msg.serialize();
        auto deserialized = ChunkDataMessage::deserialize(serialized);
        
        EXPECT_EQ(msg.data.size(), deserialized.data.size());
        if (!msg.data.empty()) {
            EXPECT_EQ(msg.data, deserialized.data);
        }
    }
}

// Test error conditions and malformed messages
TEST_F(FileProtocolTest, ErrorHandling_TruncatedMessages) {
    FileAnnounceMessage msg;
    msg.file_id = "test";
    msg.filename = "test.txt";
    msg.file_size = 1024;
    msg.file_hash = "hash";
    
    auto serialized = msg.serialize();
    
    // Test with truncated data
    std::vector<uint8_t> truncated(serialized.begin(), serialized.begin() + serialized.size() / 2);
    
    EXPECT_THROW(FileAnnounceMessage::deserialize(truncated), std::runtime_error);
}

TEST_F(FileProtocolTest, ErrorHandling_EmptyMessages) {
    std::vector<uint8_t> empty_data;
    
    EXPECT_THROW(FileAnnounceMessage::deserialize(empty_data), std::runtime_error);
    EXPECT_THROW(FileRequestMessage::deserialize(empty_data), std::runtime_error);
    EXPECT_THROW(ChunkDataMessage::deserialize(empty_data), std::runtime_error);
}

TEST_F(FileProtocolTest, ErrorHandling_InvalidStrings) {
    // Create message with invalid UTF-8 strings
    std::vector<uint8_t> malformed_data = {
        0x00, 0x00, 0x00, 0x04, // String length = 4
        0xFF, 0xFE, 0xFD, 0xFC  // Invalid UTF-8 bytes
    };
    
    // Should handle gracefully (implementation dependent)
    // Most implementations will either accept as-is or replace with replacement characters
}

// Test MessagePayload concept compliance
TEST_F(FileProtocolTest, MessagePayload_ConceptCompliance) {
    // Test that all file protocol messages satisfy the MessagePayload concept
    static_assert(MessagePayload<FileAnnounceMessage>);
    static_assert(MessagePayload<FileRequestMessage>);
    static_assert(MessagePayload<FileResponseMessage>);
    static_assert(MessagePayload<ChunkRequestMessage>);
    static_assert(MessagePayload<ChunkDataMessage>);
    static_assert(MessagePayload<ChunkAckMessage>);
}

// Performance tests
TEST_F(FileProtocolTest, Performance_SerializationSpeed) {
    // Test serialization performance with realistic data
    ChunkDataMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 0;
    msg.data = std::vector<uint8_t>(65536, 0x42); // 64KB chunk
    msg.chunk_hash = "performance_test_hash";
    
    const int iterations = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto serialized = msg.serialize();
        // Ensure compiler doesn't optimize away
        volatile size_t size = serialized.size();
        (void)size;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should serialize quickly (less than 100us per 64KB chunk)
    EXPECT_LT(duration.count() / iterations, 100);
}

TEST_F(FileProtocolTest, Performance_DeserializationSpeed) {
    ChunkDataMessage msg;
    msg.file_id = test_metadata_.file_id;
    msg.chunk_index = 0;
    msg.data = std::vector<uint8_t>(65536, 0x42);
    msg.chunk_hash = "performance_test_hash";
    
    auto serialized = msg.serialize();
    const int iterations = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        auto deserialized = ChunkDataMessage::deserialize(serialized);
        // Ensure compiler doesn't optimize away
        volatile size_t size = deserialized.data.size();
        (void)size;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should deserialize quickly
    EXPECT_LT(duration.count() / iterations, 100);
}

// Round-trip consistency tests
TEST_F(FileProtocolTest, RoundTrip_AllMessageTypes) {
    // Test that all message types maintain consistency through serialize/deserialize cycles
    
    // Multiple round trips should not change data
    FileAnnounceMessage announce;
    announce.file_id = "round_trip_test";
    announce.filename = "test.txt";
    announce.file_size = 12345;
    announce.file_hash = "consistent_hash";
    announce.tags = {"tag1", "tag2", "tag3"};
    
    for (int i = 0; i < 10; ++i) {
        auto serialized = announce.serialize();
        announce = FileAnnounceMessage::deserialize(serialized);
    }
    
    EXPECT_EQ(announce.file_id, "round_trip_test");
    EXPECT_EQ(announce.filename, "test.txt");
    EXPECT_EQ(announce.file_size, 12345);
    EXPECT_EQ(announce.file_hash, "consistent_hash");
    EXPECT_EQ(announce.tags.size(), 3);
}

// Test binary data handling
TEST_F(FileProtocolTest, BinaryData_ChunkHandling) {
    ChunkDataMessage msg;
    msg.file_id = "binary_test";
    msg.chunk_index = 0;
    msg.chunk_hash = "binary_hash";
    
    // Create binary data with all possible byte values
    msg.data.resize(256);
    for (int i = 0; i < 256; ++i) {
        msg.data[i] = static_cast<uint8_t>(i);
    }
    
    auto serialized = msg.serialize();
    auto deserialized = ChunkDataMessage::deserialize(serialized);
    
    EXPECT_EQ(msg.data.size(), deserialized.data.size());
    for (size_t i = 0; i < msg.data.size(); ++i) {
        EXPECT_EQ(msg.data[i], deserialized.data[i]) << "Mismatch at index " << i;
    }
}