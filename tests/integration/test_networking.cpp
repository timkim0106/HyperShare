#include <gtest/gtest.h>
#include "hypershare/network/tcp_server.hpp"
#include "hypershare/network/tcp_client.hpp"
#include "hypershare/network/udp_discovery.hpp"
#include "hypershare/network/connection_manager.hpp"
#include <thread>
#include <chrono>
#include <future>

using namespace hypershare::network;

class NetworkingIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_port = 18080; // Use non-standard port for testing
        discovery_port = 18081;
    }
    
    void TearDown() override {}
    
    std::uint16_t server_port;
    std::uint16_t discovery_port;
};

TEST_F(NetworkingIntegrationTest, TcpServerClientConnection) {
    TcpServer server(server_port);
    
    bool connection_received = false;
    bool message_received = false;
    std::string received_peer_name;
    
    server.set_connection_handler([&](std::shared_ptr<Connection> conn) {
        connection_received = true;
    });
    
    server.set_message_handler([&](std::shared_ptr<Connection> conn, const MessageHeader& header, std::vector<std::uint8_t> payload) {
        if (header.type == MessageType::HANDSHAKE) {
            auto msg = HandshakeMessage::deserialize(payload);
            received_peer_name = msg.peer_name;
            message_received = true;
        }
    });
    
    ASSERT_TRUE(server.start());
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    TcpClient client;
    bool client_connected = false;
    
    client.set_connect_handler([&](bool success, const std::string& error) {
        client_connected = success;
    });
    
    // Start client in separate thread
    std::thread client_thread([&client]() {
        client.run();
    });
    
    auto connect_future = client.connect_async("127.0.0.1", server_port);
    
    ASSERT_EQ(connect_future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(connect_future.get());
    EXPECT_TRUE(client_connected);
    
    // Give connection time to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(connection_received);
    
    // Send handshake message
    HandshakeMessage handshake{12345, 8080, "TestClient", 0};
    client.send_message(MessageType::HANDSHAKE, handshake);
    
    // Wait for message to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(message_received);
    EXPECT_EQ(received_peer_name, "TestClient");
    
    client.stop();
    client_thread.join();
    
    server.stop();
}

TEST_F(NetworkingIntegrationTest, UdpDiscoveryBasic) {
    UdpDiscovery discovery1(discovery_port);
    UdpDiscovery discovery2(discovery_port + 1);
    
    bool peer_discovered = false;
    std::uint32_t discovered_peer_id = 0;
    
    discovery1.set_peer_discovered_handler([&](const PeerInfo& peer) {
        peer_discovered = true;
        discovered_peer_id = peer.peer_id;
    });
    
    ASSERT_TRUE(discovery1.start());
    ASSERT_TRUE(discovery2.start());
    
    // Configure discovery instances
    discovery1.announce_self(11111, 8080, "Peer1");
    discovery2.announce_self(22222, 8081, "Peer2");
    
    // Give time for discovery
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Query for peers
    discovery1.query_peers();
    
    // Give time for response
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    EXPECT_TRUE(peer_discovered);
    EXPECT_EQ(discovered_peer_id, 22222);
    
    discovery1.stop();
    discovery2.stop();
}

TEST_F(NetworkingIntegrationTest, ConnectionManagerFullWorkflow) {
    ConnectionManager manager1;
    ConnectionManager manager2;
    
    manager1.set_local_info(33333, "Manager1");
    manager2.set_local_info(44444, "Manager2");
    
    ASSERT_TRUE(manager1.start(server_port, discovery_port));
    ASSERT_TRUE(manager2.start(server_port + 1, discovery_port + 1));
    
    // Give time for startup and discovery
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Manually connect manager1 to manager2
    EXPECT_TRUE(manager1.connect_to_peer("127.0.0.1", server_port + 1));
    
    // Give time for connection and handshake
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto connections1 = manager1.get_connections();
    auto connections2 = manager2.get_connections();
    
    EXPECT_GE(connections1.size(), 1);
    EXPECT_GE(connections2.size(), 1);
    
    // Test message sending
    HeartbeatMessage heartbeat{123456789ULL, 1, 0};
    EXPECT_TRUE(manager1.send_to_peer(44444, MessageType::HEARTBEAT, heartbeat));
    
    // Give time for message delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    manager1.stop();
    manager2.stop();
}

TEST_F(NetworkingIntegrationTest, MessageBroadcast) {
    TcpServer server(server_port);
    
    std::vector<std::shared_ptr<Connection>> connections;
    std::atomic<int> message_count{0};
    
    server.set_connection_handler([&](std::shared_ptr<Connection> conn) {
        connections.push_back(conn);
    });
    
    server.set_message_handler([&](std::shared_ptr<Connection> conn, const MessageHeader& header, std::vector<std::uint8_t> payload) {
        if (header.type == MessageType::HEARTBEAT) {
            message_count++;
        }
    });
    
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create multiple clients
    std::vector<std::unique_ptr<TcpClient>> clients;
    std::vector<std::thread> client_threads;
    
    for (int i = 0; i < 3; ++i) {
        auto client = std::make_unique<TcpClient>();
        
        auto connect_future = client->connect_async("127.0.0.1", server_port);
        ASSERT_EQ(connect_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_TRUE(connect_future.get());
        
        client_threads.emplace_back([&client]() {
            client->run();
        });
        
        clients.push_back(std::move(client));
    }
    
    // Give time for all connections
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(connections.size(), 3);
    
    // Broadcast a message
    HeartbeatMessage heartbeat{987654321ULL, 3, 10};
    server.broadcast_message(MessageType::HEARTBEAT, heartbeat);
    
    // Give time for message delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Each client should have received the broadcast
    EXPECT_EQ(message_count.load(), 3);
    
    // Cleanup
    for (auto& client : clients) {
        client->stop();
    }
    
    for (auto& thread : client_threads) {
        thread.join();
    }
    
    server.stop();
}

TEST_F(NetworkingIntegrationTest, ConnectionRecovery) {
    TcpServer server(server_port);
    
    std::atomic<int> connection_count{0};
    std::atomic<int> disconnection_count{0};
    
    server.set_connection_handler([&](std::shared_ptr<Connection> conn) {
        connection_count++;
        
        conn->set_disconnect_handler([&](std::shared_ptr<Connection> conn) {
            disconnection_count++;
        });
    });
    
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    TcpClient client;
    std::thread client_thread;
    
    // Connect
    auto connect_future = client.connect_async("127.0.0.1", server_port);
    ASSERT_EQ(connect_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(connect_future.get());
    
    client_thread = std::thread([&client]() {
        client.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(connection_count.load(), 1);
    
    // Disconnect
    client.disconnect();
    client.stop();
    client_thread.join();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(disconnection_count.load(), 1);
    
    // Reconnect
    TcpClient client2;
    connect_future = client2.connect_async("127.0.0.1", server_port);
    ASSERT_EQ(connect_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(connect_future.get());
    
    std::thread client2_thread([&client2]() {
        client2.run();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(connection_count.load(), 2);
    
    client2.stop();
    client2_thread.join();
    
    server.stop();
}