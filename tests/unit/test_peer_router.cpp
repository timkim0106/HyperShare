#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "hypershare/network/peer_router.hpp"
#include "hypershare/network/connection.hpp"
#include <thread>
#include <chrono>

using namespace hypershare::network;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

namespace {
    constexpr std::uint32_t LOCAL_PEER_ID = 1000;
    constexpr std::uint32_t REMOTE_PEER_ID_1 = 2000;
    constexpr std::uint32_t REMOTE_PEER_ID_2 = 3000;
    constexpr std::uint32_t REMOTE_PEER_ID_3 = 4000;
}

class MockConnection : public Connection {
public:
    MOCK_METHOD(bool, is_connected, (), (const, override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(std::size_t, send, (const std::vector<std::uint8_t>&), (override));
    MOCK_METHOD(std::vector<std::uint8_t>, receive, (std::size_t), (override));
    MOCK_METHOD(void, set_receive_callback, (std::function<void(const std::vector<std::uint8_t>&)>), (override));
    MOCK_METHOD(std::string, get_remote_address, (), (const, override));
    MOCK_METHOD(std::uint16_t, get_remote_port, (), (const, override));
    MOCK_METHOD(ConnectionStats, get_stats, (), (const, override));
};

class PeerRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        router_ = std::make_unique<PeerRouter>(LOCAL_PEER_ID);
        
        // Set up message capture
        captured_messages_.clear();
        captured_broadcasts_.clear();
        
        router_->set_message_sender([this](std::uint32_t peer_id, MessageType type, const std::vector<std::uint8_t>& payload) {
            captured_messages_.emplace_back(peer_id, type, payload);
        });
        
        router_->set_broadcast_sender([this](MessageType type, const std::vector<std::uint8_t>& payload) {
            captured_broadcasts_.emplace_back(type, payload);
        });
        
        router_->start();
    }
    
    void TearDown() override {
        if (router_) {
            router_->stop();
        }
    }
    
    std::unique_ptr<PeerRouter> router_;
    std::vector<std::tuple<std::uint32_t, MessageType, std::vector<std::uint8_t>>> captured_messages_;
    std::vector<std::pair<MessageType, std::vector<std::uint8_t>>> captured_broadcasts_;
};

// Test basic router creation and lifecycle
TEST_F(PeerRouterTest, BasicLifecycle) {
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.total_peers, 0);
    EXPECT_EQ(stats.direct_peers, 0);
    EXPECT_EQ(stats.known_files, 0);
    EXPECT_EQ(stats.route_entries, 0);
    EXPECT_EQ(stats.messages_forwarded, 0);
    EXPECT_EQ(stats.queries_processed, 0);
    EXPECT_EQ(stats.average_hop_count, 0.0);
}

// Test direct peer management
TEST_F(PeerRouterTest, DirectPeerManagement) {
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add direct peer
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.total_peers, 1);
    EXPECT_EQ(stats.direct_peers, 1);
    EXPECT_EQ(stats.route_entries, 1);
    
    auto peers = router_->get_known_peers();
    ASSERT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0].peer_id, REMOTE_PEER_ID_1);
    EXPECT_EQ(peers[0].ip_address, "192.168.1.100");
    EXPECT_EQ(peers[0].port, 8080);
    EXPECT_EQ(peers[0].hop_count, 1);
    EXPECT_TRUE(peers[0].is_direct());
    EXPECT_EQ(peers[0].next_hop_peer_id, REMOTE_PEER_ID_1);
    
    auto routes = router_->get_routing_table();
    ASSERT_EQ(routes.size(), 1);
    EXPECT_EQ(routes[0].destination_peer_id, REMOTE_PEER_ID_1);
    EXPECT_EQ(routes[0].next_hop_peer_id, REMOTE_PEER_ID_1);
    EXPECT_EQ(routes[0].hop_count, 1);
    
    // Test route lookup
    auto next_hop = router_->get_next_hop(REMOTE_PEER_ID_1);
    ASSERT_TRUE(next_hop.has_value());
    EXPECT_EQ(*next_hop, REMOTE_PEER_ID_1);
    
    // Remove peer
    router_->remove_peer(REMOTE_PEER_ID_1);
    stats = router_->get_statistics();
    EXPECT_EQ(stats.total_peers, 0);
    EXPECT_EQ(stats.direct_peers, 0);
    EXPECT_EQ(stats.route_entries, 0);
    
    next_hop = router_->get_next_hop(REMOTE_PEER_ID_1);
    EXPECT_FALSE(next_hop.has_value());
}

// Test file announcement and discovery
TEST_F(PeerRouterTest, FileManagement) {
    const std::string file_id = "test_file_123";
    const std::string file_hash = "abcdef0123456789";
    const std::uint64_t file_size = 1024 * 1024; // 1MB
    
    // Announce file
    router_->announce_file(file_id, file_hash, file_size);
    
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.known_files, 1);
    
    auto locations = router_->get_file_locations(file_id);
    ASSERT_EQ(locations.size(), 1);
    EXPECT_EQ(locations[0].file_id, file_id);
    EXPECT_EQ(locations[0].peer_id, LOCAL_PEER_ID);
    EXPECT_EQ(locations[0].file_hash, file_hash);
    EXPECT_EQ(locations[0].file_size, file_size);
    EXPECT_EQ(locations[0].availability_score, 1.0);
    
    // Should have broadcast file announcement
    ASSERT_EQ(captured_broadcasts_.size(), 1);
    EXPECT_EQ(captured_broadcasts_[0].first, MessageType::FILE_ANNOUNCE);
    
    // Remove file
    router_->remove_file(file_id);
    stats = router_->get_statistics();
    EXPECT_EQ(stats.known_files, 0);
    
    locations = router_->get_file_locations(file_id);
    EXPECT_TRUE(locations.empty());
}

// Test optimal peer selection for files
TEST_F(PeerRouterTest, OptimalPeerSelection) {
    const std::string file_id = "test_file_multi";
    
    auto mock_connection1 = std::make_shared<MockConnection>();
    auto mock_connection2 = std::make_shared<MockConnection>();
    auto mock_connection3 = std::make_shared<MockConnection>();
    
    EXPECT_CALL(*mock_connection1, is_connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_connection2, is_connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_connection3, is_connected()).WillRepeatedly(Return(true));
    
    // Add peers with different hop counts
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection1);
    router_->add_direct_peer(REMOTE_PEER_ID_2, "192.168.1.101", 8080, mock_connection2);
    router_->add_direct_peer(REMOTE_PEER_ID_3, "192.168.1.102", 8080, mock_connection3);
    
    // Create file query response with multiple locations
    FileQueryResponseMessage response;
    response.query_id = 12345;
    response.responding_peer_id = REMOTE_PEER_ID_1;
    
    // Add locations with different availability scores
    FileLocation loc1;
    loc1.file_id = file_id;
    loc1.peer_id = REMOTE_PEER_ID_1;
    loc1.file_hash = "hash1";
    loc1.file_size = 1000;
    loc1.announced_at = std::chrono::steady_clock::now();
    loc1.availability_score = 0.9;
    
    FileLocation loc2;
    loc2.file_id = file_id;
    loc2.peer_id = REMOTE_PEER_ID_2;
    loc2.file_hash = "hash2";
    loc2.file_size = 1000;
    loc2.announced_at = std::chrono::steady_clock::now();
    loc2.availability_score = 0.8;
    
    FileLocation loc3;
    loc3.file_id = file_id;
    loc3.peer_id = REMOTE_PEER_ID_3;
    loc3.file_hash = "hash3";
    loc3.file_size = 1000;
    loc3.announced_at = std::chrono::steady_clock::now();
    loc3.availability_score = 0.7;
    
    response.file_locations = {loc1, loc2, loc3};
    
    // Handle the response to populate file locations
    router_->handle_file_query_response(nullptr, response);
    
    // Get optimal peers (should be sorted by availability score)
    auto optimal_peers = router_->get_optimal_peers_for_file(file_id, 2);
    ASSERT_EQ(optimal_peers.size(), 2);
    EXPECT_EQ(optimal_peers[0], REMOTE_PEER_ID_1); // Highest availability
    EXPECT_EQ(optimal_peers[1], REMOTE_PEER_ID_2); // Second highest
}

// Test RouteUpdateMessage serialization/deserialization
TEST_F(PeerRouterTest, RouteUpdateSerialization) {
    RouteUpdateMessage original;
    original.source_peer_id = LOCAL_PEER_ID;
    original.sequence_number = 42;
    original.hop_count = 2;
    
    RoutingPeerInfo peer;
    peer.peer_id = REMOTE_PEER_ID_1;
    peer.ip_address = "192.168.1.100";
    peer.port = 8080;
    peer.last_seen = std::chrono::steady_clock::now();
    peer.hop_count = 3;
    peer.next_hop_peer_id = REMOTE_PEER_ID_2;
    peer.reliability_score = 0.85;
    peer.bandwidth_estimate = 5000000; // 5MB/s
    
    original.peer_updates.push_back(peer);
    
    // Serialize
    auto serialized = original.serialize();
    EXPECT_GT(serialized.size(), 0);
    
    // Deserialize
    auto deserialized = RouteUpdateMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.source_peer_id, original.source_peer_id);
    EXPECT_EQ(deserialized.sequence_number, original.sequence_number);
    EXPECT_EQ(deserialized.hop_count, original.hop_count);
    ASSERT_EQ(deserialized.peer_updates.size(), 1);
    
    const auto& deserialized_peer = deserialized.peer_updates[0];
    EXPECT_EQ(deserialized_peer.peer_id, peer.peer_id);
    EXPECT_EQ(deserialized_peer.ip_address, peer.ip_address);
    EXPECT_EQ(deserialized_peer.port, peer.port);
    EXPECT_EQ(deserialized_peer.hop_count, peer.hop_count);
    EXPECT_EQ(deserialized_peer.next_hop_peer_id, peer.next_hop_peer_id);
    EXPECT_NEAR(deserialized_peer.reliability_score, peer.reliability_score, 0.000001);
    EXPECT_EQ(deserialized_peer.bandwidth_estimate, peer.bandwidth_estimate);
}

// Test TopologySyncMessage serialization/deserialization
TEST_F(PeerRouterTest, TopologySyncSerialization) {
    TopologySyncMessage original;
    original.requesting_peer_id = LOCAL_PEER_ID;
    original.last_known_sequence = 123456789;
    original.known_peers = {REMOTE_PEER_ID_1, REMOTE_PEER_ID_2, REMOTE_PEER_ID_3};
    
    // Serialize
    auto serialized = original.serialize();
    EXPECT_GT(serialized.size(), 0);
    
    // Deserialize
    auto deserialized = TopologySyncMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.requesting_peer_id, original.requesting_peer_id);
    EXPECT_EQ(deserialized.last_known_sequence, original.last_known_sequence);
    EXPECT_EQ(deserialized.known_peers, original.known_peers);
}

// Test FileQueryMessage serialization/deserialization
TEST_F(PeerRouterTest, FileQuerySerialization) {
    FileQueryMessage original;
    original.file_id = "test_file_query";
    original.query_hash = "query_hash_123";
    original.source_peer_id = LOCAL_PEER_ID;
    original.query_id = 98765;
    original.hop_count = 3;
    original.search_terms = {"term1", "term2", "term3"};
    
    // Serialize
    auto serialized = original.serialize();
    EXPECT_GT(serialized.size(), 0);
    
    // Deserialize
    auto deserialized = FileQueryMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.file_id, original.file_id);
    EXPECT_EQ(deserialized.query_hash, original.query_hash);
    EXPECT_EQ(deserialized.source_peer_id, original.source_peer_id);
    EXPECT_EQ(deserialized.query_id, original.query_id);
    EXPECT_EQ(deserialized.hop_count, original.hop_count);
    EXPECT_EQ(deserialized.search_terms, original.search_terms);
}

// Test FileQueryResponseMessage serialization/deserialization
TEST_F(PeerRouterTest, FileQueryResponseSerialization) {
    FileQueryResponseMessage original;
    original.query_id = 12345;
    original.responding_peer_id = REMOTE_PEER_ID_1;
    
    FileLocation location;
    location.file_id = "test_file_response";
    location.peer_id = REMOTE_PEER_ID_2;
    location.file_hash = "response_hash";
    location.file_size = 2048;
    location.announced_at = std::chrono::steady_clock::now();
    location.availability_score = 0.95;
    
    original.file_locations.push_back(location);
    
    // Serialize
    auto serialized = original.serialize();
    EXPECT_GT(serialized.size(), 0);
    
    // Deserialize
    auto deserialized = FileQueryResponseMessage::deserialize(serialized);
    
    EXPECT_EQ(deserialized.query_id, original.query_id);
    EXPECT_EQ(deserialized.responding_peer_id, original.responding_peer_id);
    ASSERT_EQ(deserialized.file_locations.size(), 1);
    
    const auto& deserialized_location = deserialized.file_locations[0];
    EXPECT_EQ(deserialized_location.file_id, location.file_id);
    EXPECT_EQ(deserialized_location.peer_id, location.peer_id);
    EXPECT_EQ(deserialized_location.file_hash, location.file_hash);
    EXPECT_EQ(deserialized_location.file_size, location.file_size);
    EXPECT_NEAR(deserialized_location.availability_score, location.availability_score, 0.000001);
}

// Test route update handling
TEST_F(PeerRouterTest, RouteUpdateHandling) {
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add direct peer
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    
    // Create route update from peer 1 about peer 2
    RouteUpdateMessage update;
    update.source_peer_id = REMOTE_PEER_ID_1;
    update.sequence_number = 1;
    update.hop_count = 1;
    
    RoutingPeerInfo peer2_info;
    peer2_info.peer_id = REMOTE_PEER_ID_2;
    peer2_info.ip_address = "192.168.1.101";
    peer2_info.port = 8080;
    peer2_info.last_seen = std::chrono::steady_clock::now();
    peer2_info.hop_count = 1; // Direct connection from peer 1's perspective
    peer2_info.next_hop_peer_id = REMOTE_PEER_ID_2;
    peer2_info.reliability_score = 0.9;
    peer2_info.bandwidth_estimate = 10000000;
    
    update.peer_updates.push_back(peer2_info);
    
    // Handle the update
    router_->handle_route_update(mock_connection, update);
    
    // Should now know about peer 2 via peer 1
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.total_peers, 2); // Peer 1 (direct) + Peer 2 (via peer 1)
    EXPECT_EQ(stats.route_entries, 2);
    
    auto next_hop = router_->get_next_hop(REMOTE_PEER_ID_2);
    ASSERT_TRUE(next_hop.has_value());
    EXPECT_EQ(*next_hop, REMOTE_PEER_ID_1); // Route to peer 2 via peer 1
    
    auto peers = router_->get_known_peers();
    auto peer2_it = std::find_if(peers.begin(), peers.end(),
        [](const RoutingPeerInfo& p) { return p.peer_id == REMOTE_PEER_ID_2; });
    ASSERT_NE(peer2_it, peers.end());
    EXPECT_EQ(peer2_it->hop_count, 2); // 1 hop from peer 1 + 1 hop to reach peer 1
    EXPECT_EQ(peer2_it->next_hop_peer_id, REMOTE_PEER_ID_1);
}

// Test message forwarding
TEST_F(PeerRouterTest, MessageForwarding) {
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add direct peer
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    
    // Create route to peer 2 via peer 1
    RouteUpdateMessage update;
    update.source_peer_id = REMOTE_PEER_ID_1;
    update.sequence_number = 1;
    update.hop_count = 1;
    
    PeerInfo peer2_info;
    peer2_info.peer_id = REMOTE_PEER_ID_2;
    peer2_info.ip_address = "192.168.1.101";
    peer2_info.port = 8080;
    peer2_info.last_seen = std::chrono::steady_clock::now();
    peer2_info.hop_count = 1;
    peer2_info.next_hop_peer_id = REMOTE_PEER_ID_2;
    peer2_info.reliability_score = 0.9;
    peer2_info.bandwidth_estimate = 10000000;
    
    update.peer_updates.push_back(peer2_info);
    router_->handle_route_update(mock_connection, update);
    
    // Clear previous captured messages
    captured_messages_.clear();
    
    // Forward message to peer 2
    std::vector<std::uint8_t> test_payload = {0x01, 0x02, 0x03, 0x04};
    bool forwarded = router_->forward_message(REMOTE_PEER_ID_2, MessageType::HEARTBEAT, test_payload);
    
    EXPECT_TRUE(forwarded);
    ASSERT_EQ(captured_messages_.size(), 1);
    
    auto [target_peer, msg_type, payload] = captured_messages_[0];
    EXPECT_EQ(target_peer, REMOTE_PEER_ID_1); // Should forward to peer 1 (next hop)
    EXPECT_EQ(msg_type, MessageType::HEARTBEAT);
    EXPECT_EQ(payload, test_payload);
    
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.messages_forwarded, 1);
}

// Test file query handling
TEST_F(PeerRouterTest, FileQueryHandling) {
    const std::string file_id = "query_test_file";
    const std::string file_hash = "query_test_hash";
    const std::uint64_t file_size = 512;
    
    // Announce a local file
    router_->announce_file(file_id, file_hash, file_size);
    
    // Clear broadcasts from file announcement
    captured_messages_.clear();
    captured_broadcasts_.clear();
    
    // Create file query
    FileQueryMessage query;
    query.file_id = file_id;
    query.query_hash = "test_query_hash";
    query.source_peer_id = REMOTE_PEER_ID_1;
    query.query_id = 55555;
    query.hop_count = 1;
    query.search_terms = {"test"};
    
    // Handle the query
    router_->handle_file_query(nullptr, query);
    
    // Should respond with file location since we have the file
    ASSERT_EQ(captured_messages_.size(), 1);
    
    auto [target_peer, msg_type, payload] = captured_messages_[0];
    EXPECT_EQ(target_peer, REMOTE_PEER_ID_1);
    EXPECT_EQ(msg_type, MessageType::ROUTE_UPDATE); // Using ROUTE_UPDATE as proxy
    
    // Deserialize the response
    auto response = FileQueryResponseMessage::deserialize(payload);
    EXPECT_EQ(response.query_id, query.query_id);
    EXPECT_EQ(response.responding_peer_id, LOCAL_PEER_ID);
    ASSERT_EQ(response.file_locations.size(), 1);
    EXPECT_EQ(response.file_locations[0].file_id, file_id);
    EXPECT_EQ(response.file_locations[0].peer_id, LOCAL_PEER_ID);
    
    auto stats = router_->get_statistics();
    EXPECT_EQ(stats.queries_processed, 1);
}

// Test query deduplication
TEST_F(PeerRouterTest, QueryDeduplication) {
    FileQueryMessage query;
    query.file_id = "dedup_test_file";
    query.query_hash = "dedup_hash";
    query.source_peer_id = REMOTE_PEER_ID_1;
    query.query_id = 77777;
    query.hop_count = 1;
    
    // Handle query first time
    router_->handle_file_query(nullptr, query);
    auto stats1 = router_->get_statistics();
    
    // Handle same query again immediately
    router_->handle_file_query(nullptr, query);
    auto stats2 = router_->get_statistics();
    
    // Query count should not increase (deduplicated)
    EXPECT_EQ(stats1.queries_processed, stats2.queries_processed);
}

// Test topology sync handling
TEST_F(PeerRouterTest, TopologySyncHandling) {
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add some known peers
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    router_->add_direct_peer(REMOTE_PEER_ID_2, "192.168.1.101", 8080, mock_connection);
    
    // Clear any initial messages
    captured_messages_.clear();
    
    // Create topology sync request
    TopologySyncMessage sync_request;
    sync_request.requesting_peer_id = REMOTE_PEER_ID_3;
    sync_request.last_known_sequence = 0;
    sync_request.known_peers = {};
    
    // Handle the sync request
    router_->handle_topology_sync(nullptr, sync_request);
    
    // Should respond with route update containing our known peers
    ASSERT_EQ(captured_messages_.size(), 1);
    
    auto [target_peer, msg_type, payload] = captured_messages_[0];
    EXPECT_EQ(target_peer, REMOTE_PEER_ID_3);
    EXPECT_EQ(msg_type, MessageType::ROUTE_UPDATE);
    
    // Deserialize the response
    auto response = RouteUpdateMessage::deserialize(payload);
    EXPECT_EQ(response.source_peer_id, LOCAL_PEER_ID);
    EXPECT_GE(response.peer_updates.size(), 2); // Should include peer 1 and 2 (excluding requester)
}

// Test reliability scoring and metric calculation
TEST_F(PeerRouterTest, ReliabilityScoring) {
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add direct peer
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    
    // Get initial reliability
    auto peers = router_->get_known_peers();
    ASSERT_EQ(peers.size(), 1);
    double initial_reliability = peers[0].reliability_score;
    EXPECT_EQ(initial_reliability, 1.0);
    
    // This test would require access to private methods for full testing
    // In a real implementation, we might expose update_peer_reliability as protected
    // or create a test-friendly interface
    
    // For now, just verify the peer exists and has expected initial values
    auto routes = router_->get_routing_table();
    ASSERT_EQ(routes.size(), 1);
    EXPECT_GT(routes[0].metric, 0.0); // Should have calculated some metric
}

// Test concurrent operations
TEST_F(PeerRouterTest, ConcurrentOperations) {
    constexpr int NUM_THREADS = 5;
    constexpr int OPERATIONS_PER_THREAD = 10;
    
    std::vector<std::thread> threads;
    std::atomic<int> completed_operations{0};
    
    // Launch threads that perform various operations concurrently
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t, &completed_operations]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                std::uint32_t peer_id = 5000 + t * 1000 + i;
                std::string ip = "192.168.1." + std::to_string(100 + t);
                
                auto mock_connection = std::make_shared<MockConnection>();
                EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
                
                // Add peer
                router_->add_direct_peer(peer_id, ip, 8080, mock_connection);
                
                // Announce file
                std::string file_id = "file_" + std::to_string(t) + "_" + std::to_string(i);
                router_->announce_file(file_id, "hash_" + file_id, 1024);
                
                // Query for file
                router_->find_file(file_id);
                
                // Remove peer
                router_->remove_peer(peer_id);
                
                completed_operations++;
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(completed_operations.load(), NUM_THREADS * OPERATIONS_PER_THREAD);
    
    // Router should still be in a consistent state
    auto stats = router_->get_statistics();
    // All peers should be removed, but some files might remain
    EXPECT_EQ(stats.direct_peers, 0);
}

// Test error handling for malformed messages
TEST_F(PeerRouterTest, MalformedMessageHandling) {
    // Test with insufficient data
    std::vector<std::uint8_t> malformed_data = {0x01, 0x02}; // Too short
    
    EXPECT_THROW(RouteUpdateMessage::deserialize(malformed_data), std::runtime_error);
    EXPECT_THROW(TopologySyncMessage::deserialize(malformed_data), std::runtime_error);
    EXPECT_THROW(FileQueryMessage::deserialize(malformed_data), std::runtime_error);
    EXPECT_THROW(FileQueryResponseMessage::deserialize(malformed_data), std::runtime_error);
}

// Test maintenance operations
TEST_F(PeerRouterTest, MaintenanceOperations) {
    // This test verifies that maintenance operations don't crash
    // Full testing of maintenance would require time manipulation or access to private methods
    
    auto mock_connection = std::make_shared<MockConnection>();
    EXPECT_CALL(*mock_connection, is_connected()).WillRepeatedly(Return(true));
    
    // Add peer and file
    router_->add_direct_peer(REMOTE_PEER_ID_1, "192.168.1.100", 8080, mock_connection);
    router_->announce_file("test_file", "test_hash", 1024);
    
    // Let maintenance run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Router should still be functional
    auto stats = router_->get_statistics();
    EXPECT_GE(stats.total_peers, 0);
    EXPECT_GE(stats.known_files, 0);
}