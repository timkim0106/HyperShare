#pragma once

#include "hypershare/network/protocol.hpp"
#include "hypershare/network/connection.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>

namespace hypershare::network {

constexpr std::uint8_t MAX_HOP_COUNT = 16;
constexpr std::chrono::minutes ROUTE_TIMEOUT{30};
constexpr std::chrono::seconds TOPOLOGY_UPDATE_INTERVAL{60};

struct RoutingPeerInfo {
    std::uint32_t peer_id;
    std::string ip_address;
    std::uint16_t port;
    std::chrono::steady_clock::time_point last_seen;
    std::uint8_t hop_count;
    std::uint32_t next_hop_peer_id;
    double reliability_score;
    std::uint64_t bandwidth_estimate;
    
    bool is_expired() const {
        return std::chrono::steady_clock::now() - last_seen > ROUTE_TIMEOUT;
    }
    
    bool is_direct() const {
        return hop_count == 1;
    }
};

struct RouteEntry {
    std::uint32_t destination_peer_id;
    std::uint32_t next_hop_peer_id;
    std::uint8_t hop_count;
    std::chrono::steady_clock::time_point last_updated;
    double metric;
    
    bool is_expired() const {
        return std::chrono::steady_clock::now() - last_updated > ROUTE_TIMEOUT;
    }
};

struct FileLocation {
    std::string file_id;
    std::uint32_t peer_id;
    std::string file_hash;
    std::uint64_t file_size;
    std::chrono::steady_clock::time_point announced_at;
    double availability_score;
};

struct RouteUpdateMessage {
    std::uint32_t source_peer_id;
    std::vector<RoutingPeerInfo> peer_updates;
    std::uint64_t sequence_number;
    std::uint8_t hop_count;
    
    std::vector<std::uint8_t> serialize() const;
    static RouteUpdateMessage deserialize(std::span<const std::uint8_t> data);
};

struct TopologySyncMessage {
    std::uint32_t requesting_peer_id;
    std::uint64_t last_known_sequence;
    std::vector<std::uint32_t> known_peers;
    
    std::vector<std::uint8_t> serialize() const;
    static TopologySyncMessage deserialize(std::span<const std::uint8_t> data);
};

struct FileQueryMessage {
    std::string file_id;
    std::string query_hash;
    std::uint32_t source_peer_id;
    std::uint32_t query_id;
    std::uint8_t hop_count;
    std::vector<std::string> search_terms;
    
    std::vector<std::uint8_t> serialize() const;
    static FileQueryMessage deserialize(std::span<const std::uint8_t> data);
};

struct FileQueryResponseMessage {
    std::uint32_t query_id;
    std::vector<FileLocation> file_locations;
    std::uint32_t responding_peer_id;
    
    std::vector<std::uint8_t> serialize() const;
    static FileQueryResponseMessage deserialize(std::span<const std::uint8_t> data);
};

class PeerRouter {
public:
    explicit PeerRouter(std::uint32_t local_peer_id);
    ~PeerRouter();
    
    void start();
    void stop();
    
    void add_direct_peer(std::uint32_t peer_id, const std::string& ip, std::uint16_t port, 
                        std::shared_ptr<Connection> connection);
    void remove_peer(std::uint32_t peer_id);
    
    void announce_file(const std::string& file_id, const std::string& file_hash, 
                      std::uint64_t file_size);
    void remove_file(const std::string& file_id);
    
    std::vector<FileLocation> find_file(const std::string& file_id, 
                                       const std::vector<std::string>& search_terms = {});
    
    std::optional<std::uint32_t> get_next_hop(std::uint32_t destination_peer_id) const;
    std::vector<std::uint32_t> get_optimal_peers_for_file(const std::string& file_id, 
                                                         std::size_t max_peers = 3) const;
    
    bool forward_message(std::uint32_t destination_peer_id, MessageType type, 
                        const std::vector<std::uint8_t>& payload);
    
    void handle_route_update(std::shared_ptr<Connection> connection, 
                           const RouteUpdateMessage& message);
    void handle_topology_sync(std::shared_ptr<Connection> connection, 
                            const TopologySyncMessage& message);
    void handle_file_query(std::shared_ptr<Connection> connection, 
                          const FileQueryMessage& message);
    void handle_file_query_response(std::shared_ptr<Connection> connection, 
                                   const FileQueryResponseMessage& message);
    
    std::vector<RoutingPeerInfo> get_known_peers() const;
    std::vector<RouteEntry> get_routing_table() const;
    std::vector<FileLocation> get_file_locations(const std::string& file_id = "") const;
    
    void set_message_sender(std::function<void(std::uint32_t, MessageType, const std::vector<std::uint8_t>&)> sender);
    void set_broadcast_sender(std::function<void(MessageType, const std::vector<std::uint8_t>&)> sender);
    
    struct Statistics {
        std::size_t total_peers;
        std::size_t direct_peers;
        std::size_t known_files;
        std::size_t route_entries;
        std::uint64_t messages_forwarded;
        std::uint64_t queries_processed;
        double average_hop_count;
    };
    
    Statistics get_statistics() const;

private:
    void routing_maintenance_loop();
    void send_route_updates();
    void send_topology_sync();
    void cleanup_expired_entries();
    void update_peer_reliability(std::uint32_t peer_id, bool success);
    double calculate_route_metric(const RoutingPeerInfo& peer) const;
    void rebuild_routing_table();
    std::vector<std::uint32_t> get_flooding_targets(std::uint32_t source_peer_id, 
                                                   std::uint8_t max_hops) const;
    
    std::uint32_t local_peer_id_;
    mutable std::mutex routing_mutex_;
    mutable std::mutex file_mutex_;
    
    std::unordered_map<std::uint32_t, RoutingPeerInfo> known_peers_;
    std::unordered_map<std::uint32_t, std::shared_ptr<Connection>> direct_connections_;
    std::unordered_map<std::uint32_t, RouteEntry> routing_table_;
    std::unordered_map<std::string, std::vector<FileLocation>> file_locations_;
    std::unordered_set<std::string> local_files_;
    
    std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> query_cache_;
    
    std::function<void(std::uint32_t, MessageType, const std::vector<std::uint8_t>&)> message_sender_;
    std::function<void(MessageType, const std::vector<std::uint8_t>&)> broadcast_sender_;
    
    std::thread maintenance_thread_;
    bool running_;
    
    std::uint64_t route_sequence_number_;
    
    mutable std::mutex stats_mutex_;
    Statistics stats_;
};

}