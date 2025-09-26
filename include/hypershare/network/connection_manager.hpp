#pragma once

#include "hypershare/network/network_manager.hpp"
#include "hypershare/network/udp_discovery.hpp"
#include <memory>
#include <unordered_set>
#include <chrono>

namespace hypershare::storage {
    class FileIndex;
}

namespace hypershare::network {

class FileAnnouncer;

enum class HandshakeState {
    NONE,
    SENT,
    RECEIVED,
    COMPLETED,
    FAILED
};

struct ConnectionInfo {
    std::shared_ptr<Connection> connection;
    HandshakeState handshake_state;
    std::chrono::steady_clock::time_point last_heartbeat;
    std::chrono::steady_clock::time_point connected_at;
    std::uint32_t peer_id;
    std::string peer_name;
    std::uint32_t capabilities;
    bool is_outgoing;
};

class ConnectionManager : public std::enable_shared_from_this<ConnectionManager> {
public:
    ConnectionManager();
    ~ConnectionManager();
    
    bool start(std::uint16_t tcp_port, std::uint16_t udp_port = 8081);
    void stop();
    
    void set_local_info(std::uint32_t peer_id, const std::string& peer_name);
    
    bool connect_to_peer(const std::string& host, std::uint16_t port);
    void disconnect_from_peer(std::uint32_t peer_id);
    void disconnect_all();
    
    std::vector<ConnectionInfo> get_connections() const;
    std::optional<ConnectionInfo> get_connection_info(std::uint32_t peer_id) const;
    std::size_t get_connection_count() const;
    
    template<MessagePayload T>
    void broadcast_message(MessageType type, const T& message) {
        network_manager_->broadcast_message(type, message);
    }
    
    template<MessagePayload T>
    bool send_to_peer(std::uint32_t peer_id, MessageType type, const T& message) {
        auto info = get_connection_info(peer_id);
        if (info && info->handshake_state == HandshakeState::COMPLETED) {
            info->connection->send_message(type, message);
            return true;
        }
        return false;
    }
    
    void set_handshake_timeout(std::chrono::milliseconds timeout) { handshake_timeout_ = timeout; }
    void set_heartbeat_interval(std::chrono::milliseconds interval) { heartbeat_interval_ = interval; }
    void set_connection_timeout(std::chrono::milliseconds timeout) { connection_timeout_ = timeout; }
    
    void initialize_file_announcer(std::shared_ptr<hypershare::storage::FileIndex> file_index);
    std::shared_ptr<FileAnnouncer> get_file_announcer() const { return file_announcer_; }

private:
    void handle_new_connection(std::shared_ptr<Connection> connection);
    void handle_peer_discovered(const PeerInfo& peer);
    void handle_peer_lost(std::uint32_t peer_id);
    
    void handle_handshake(std::shared_ptr<Connection> connection, const HandshakeMessage& msg);
    void handle_handshake_ack(std::shared_ptr<Connection> connection, const HandshakeMessage& msg);
    void handle_heartbeat(std::shared_ptr<Connection> connection, const HeartbeatMessage& msg);
    void handle_file_announce(std::shared_ptr<Connection> connection, const FileAnnounceMessage& msg);
    void handle_disconnect(std::shared_ptr<Connection> connection);
    
    void send_handshake(std::shared_ptr<Connection> connection);
    void send_heartbeat(std::shared_ptr<Connection> connection);
    void check_connection_health();
    void cleanup_failed_connections();
    
    std::unique_ptr<NetworkManager> network_manager_;
    std::unique_ptr<UdpDiscovery> discovery_;
    std::shared_ptr<FileAnnouncer> file_announcer_;
    
    std::unordered_map<std::shared_ptr<Connection>, ConnectionInfo> connections_;
    std::unordered_map<std::uint32_t, std::shared_ptr<Connection>> peer_connections_;
    std::unordered_set<std::string> connecting_endpoints_;
    mutable std::mutex connections_mutex_;
    
    std::uint32_t local_peer_id_;
    std::string local_peer_name_;
    std::uint16_t local_tcp_port_;
    
    std::chrono::milliseconds handshake_timeout_;
    std::chrono::milliseconds heartbeat_interval_;
    std::chrono::milliseconds connection_timeout_;
    
    std::thread health_check_thread_;
    bool running_;
};

}