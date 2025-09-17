#include <gtest/gtest.h>
#include "hypershare/network/message_handler.hpp"
#include "hypershare/network/connection.hpp"
#include <memory>

using namespace hypershare::network;

class MessageHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        handler = std::make_unique<MessageHandler>();
        connection = nullptr; // We'll use a nullptr for testing
    }
    
    void TearDown() override {}
    
    std::unique_ptr<MessageHandler> handler;
    std::shared_ptr<Connection> connection;
};

TEST_F(MessageHandlerTest, RegisterAndHandleHeartbeat) {
    bool handler_called = false;
    HeartbeatMessage received_msg;
    
    handler->register_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
        [&](std::shared_ptr<Connection> conn, const HeartbeatMessage& msg) {
            handler_called = true;
            received_msg = msg;
        });
    
    HeartbeatMessage test_msg{
        1234567890ULL,
        3,
        50
    };
    
    auto payload = test_msg.serialize();
    MessageHeader header(MessageType::HEARTBEAT, static_cast<std::uint32_t>(payload.size()));
    
    handler->handle_message(connection, header, payload);
    
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(received_msg.timestamp, test_msg.timestamp);
    EXPECT_EQ(received_msg.active_connections, test_msg.active_connections);
    EXPECT_EQ(received_msg.available_files, test_msg.available_files);
}

TEST_F(MessageHandlerTest, RegisterAndHandleHandshake) {
    bool handler_called = false;
    HandshakeMessage received_msg;
    
    handler->register_handler<HandshakeMessage>(MessageType::HANDSHAKE,
        [&](std::shared_ptr<Connection> conn, const HandshakeMessage& msg) {
            handler_called = true;
            received_msg = msg;
        });
    
    HandshakeMessage test_msg{
        98765,
        9090,
        "TestNode",
        0x11223344
    };
    
    auto payload = test_msg.serialize();
    MessageHeader header(MessageType::HANDSHAKE, static_cast<std::uint32_t>(payload.size()));
    
    handler->handle_message(connection, header, payload);
    
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(received_msg.peer_id, test_msg.peer_id);
    EXPECT_EQ(received_msg.listen_port, test_msg.listen_port);
    EXPECT_EQ(received_msg.peer_name, test_msg.peer_name);
    EXPECT_EQ(received_msg.capabilities, test_msg.capabilities);
}

TEST_F(MessageHandlerTest, UnregisteredMessageType) {
    // Don't register any handlers
    HeartbeatMessage test_msg{123, 1, 1};
    auto payload = test_msg.serialize();
    MessageHeader header(MessageType::HEARTBEAT, static_cast<std::uint32_t>(payload.size()));
    
    // Should not crash, just log a warning
    EXPECT_NO_THROW(handler->handle_message(connection, header, payload));
}

TEST_F(MessageHandlerTest, InvalidPayloadDeserialization) {
    bool handler_called = false;
    
    handler->register_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
        [&](std::shared_ptr<Connection> conn, const HeartbeatMessage& msg) {
            handler_called = true;
        });
    
    // Send invalid payload data
    std::vector<std::uint8_t> invalid_payload = {0x01, 0x02}; // Too short for HeartbeatMessage
    MessageHeader header(MessageType::HEARTBEAT, static_cast<std::uint32_t>(invalid_payload.size()));
    
    // Should not crash, should handle the exception
    EXPECT_NO_THROW(handler->handle_message(connection, header, invalid_payload));
    EXPECT_FALSE(handler_called);
}

class MessageSerializerTest : public ::testing::Test {};

TEST_F(MessageSerializerTest, SerializeMessage) {
    HeartbeatMessage msg{9876543210ULL, 5, 25};
    
    auto serialized = MessageSerializer::serialize_message(MessageType::HEARTBEAT, msg);
    
    EXPECT_GE(serialized.size(), MESSAGE_HEADER_SIZE);
    
    auto [header, payload] = MessageSerializer::deserialize_message(serialized);
    
    EXPECT_EQ(header.type, MessageType::HEARTBEAT);
    EXPECT_EQ(header.payload_size, payload.size());
    EXPECT_TRUE(header.verify_checksum(payload));
    
    auto deserialized_msg = MessageSerializer::deserialize_payload<HeartbeatMessage>(payload);
    
    EXPECT_EQ(deserialized_msg.timestamp, msg.timestamp);
    EXPECT_EQ(deserialized_msg.active_connections, msg.active_connections);
    EXPECT_EQ(deserialized_msg.available_files, msg.available_files);
}

TEST_F(MessageSerializerTest, SerializeHandshakeMessage) {
    HandshakeMessage msg{11111, 7777, "SerializerTest", 0x55555555};
    
    auto serialized = MessageSerializer::serialize_message(MessageType::HANDSHAKE, msg);
    auto [header, payload] = MessageSerializer::deserialize_message(serialized);
    
    EXPECT_EQ(header.type, MessageType::HANDSHAKE);
    EXPECT_TRUE(header.verify_checksum(payload));
    
    auto deserialized = MessageSerializer::deserialize_payload<HandshakeMessage>(payload);
    
    EXPECT_EQ(deserialized.peer_id, msg.peer_id);
    EXPECT_EQ(deserialized.listen_port, msg.listen_port);
    EXPECT_EQ(deserialized.peer_name, msg.peer_name);
    EXPECT_EQ(deserialized.capabilities, msg.capabilities);
}

TEST_F(MessageSerializerTest, InvalidMessageData) {
    std::vector<std::uint8_t> invalid_data = {0x01, 0x02, 0x03}; // Too short
    
    EXPECT_THROW(MessageSerializer::deserialize_message(invalid_data), std::runtime_error);
}

class MessageQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue = std::make_unique<MessageQueue>(5); // Small queue for testing
    }
    
    std::unique_ptr<MessageQueue> queue;
};

TEST_F(MessageQueueTest, PushAndPop) {
    MessageHeader header(MessageType::HEARTBEAT, 10);
    std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    queue->push(header, payload);
    
    EXPECT_EQ(queue->size(), 1);
    EXPECT_FALSE(queue->empty());
    
    auto msg = queue->pop();
    
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::HEARTBEAT);
    EXPECT_EQ(msg->payload, payload);
    EXPECT_EQ(msg->retry_count, 0);
    
    EXPECT_EQ(queue->size(), 0);
    EXPECT_TRUE(queue->empty());
}

TEST_F(MessageQueueTest, PriorityMessages) {
    MessageHeader normal_header(MessageType::FILE_ANNOUNCE, 5);
    MessageHeader priority_header(MessageType::DISCONNECT, 3);
    
    std::vector<std::uint8_t> normal_payload = {1, 2, 3, 4, 5};
    std::vector<std::uint8_t> priority_payload = {6, 7, 8};
    
    queue->push(normal_header, normal_payload);
    queue->push_priority(priority_header, priority_payload);
    
    EXPECT_EQ(queue->size(), 2);
    
    // Priority message should come first
    auto msg1 = queue->pop();
    ASSERT_TRUE(msg1.has_value());
    EXPECT_EQ(msg1->header.type, MessageType::DISCONNECT);
    
    // Normal message should come second
    auto msg2 = queue->pop();
    ASSERT_TRUE(msg2.has_value());
    EXPECT_EQ(msg2->header.type, MessageType::FILE_ANNOUNCE);
}

TEST_F(MessageQueueTest, QueueOverflow) {
    MessageHeader header(MessageType::HEARTBEAT, 1);
    std::vector<std::uint8_t> payload = {42};
    
    // Fill the queue beyond capacity
    for (int i = 0; i < 10; ++i) {
        queue->push(header, payload);
    }
    
    // Should not exceed max size
    EXPECT_LE(queue->size(), 5);
}

TEST_F(MessageQueueTest, CleanupOldMessages) {
    MessageHeader header(MessageType::HEARTBEAT, 1);
    std::vector<std::uint8_t> payload = {42};
    
    queue->push(header, payload);
    
    // Cleanup with very short age should remove the message
    queue->cleanup_old_messages(std::chrono::milliseconds(1));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue->cleanup_old_messages(std::chrono::milliseconds(1));
    
    EXPECT_TRUE(queue->empty());
}

TEST_F(MessageQueueTest, ClearQueue) {
    MessageHeader header(MessageType::HEARTBEAT, 1);
    std::vector<std::uint8_t> payload = {42};
    
    queue->push(header, payload);
    queue->push(header, payload);
    queue->push_priority(header, payload);
    
    EXPECT_EQ(queue->size(), 3);
    
    queue->clear();
    
    EXPECT_TRUE(queue->empty());
    EXPECT_EQ(queue->size(), 0);
}