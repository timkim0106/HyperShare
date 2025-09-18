#include "hypershare/network/connection_manager.hpp"
#include "hypershare/core/logger.hpp"
#include <random>

namespace hypershare::network {

ConnectionManager::ConnectionManager()
    : local_peer_id_(0)
    , local_tcp_port_(0)
    , handshake_timeout_(std::chrono::seconds(10))
    , heartbeat_interval_(std::chrono::seconds(30))
    , connection_timeout_(std::chrono::minutes(2))
    , running_(false) {
    
    std::random_device rd;
    std::mt19937 gen(rd());
    local_peer_id_ = gen();
    local_peer_name_ = "HyperShare-" + std::to_string(local_peer_id_ % 10000);
    
    LOG_INFO("Connection manager initialized with peer ID: {}", local_peer_id_);
}

ConnectionManager::~ConnectionManager() {
    stop();
}

bool ConnectionManager::start(std::uint16_t tcp_port, std::uint16_t udp_port) {
    if (running_) {
        LOG_WARN("Connection manager already running");
        return false;
    }
    
    local_tcp_port_ = tcp_port;
    
    network_manager_ = std::make_unique<NetworkManager>();
    discovery_ = std::make_unique<UdpDiscovery>(udp_port);
    
    // Set up network manager handlers
    network_manager_->register_message_handler<HandshakeMessage>(MessageType::HANDSHAKE,
        [this](std::shared_ptr<Connection> conn, const HandshakeMessage& msg) {
            handle_handshake(conn, msg);
        });
    
    network_manager_->register_message_handler<HandshakeMessage>(MessageType::HANDSHAKE_ACK,
        [this](std::shared_ptr<Connection> conn, const HandshakeMessage& msg) {
            handle_handshake_ack(conn, msg);
        });
    
    network_manager_->register_message_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
        [this](std::shared_ptr<Connection> conn, const HeartbeatMessage& msg) {
            handle_heartbeat(conn, msg);
        });
    
    // Set up discovery handlers
    discovery_->set_peer_discovered_handler(
        [this](const PeerInfo& peer) {
            handle_peer_discovered(peer);
        });
    
    discovery_->set_peer_lost_handler(
        [this](std::uint32_t peer_id) {
            handle_peer_lost(peer_id);
        });
    
    // Start network components
    if (!network_manager_->start_server(tcp_port)) {
        LOG_ERROR("Failed to start TCP server on port {}", tcp_port);
        return false;
    }
    
    if (!discovery_->start()) {
        LOG_ERROR("Failed to start UDP discovery on port {}", udp_port);
        network_manager_->stop_server();
        return false;
    }
    
    // Announce ourselves
    discovery_->announce_self(local_peer_id_, tcp_port, local_peer_name_);
    
    running_ = true;
    
    // Start health check thread
    health_check_thread_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (running_) {
                check_connection_health();
                cleanup_failed_connections();
            }
        }
    });
    
    LOG_INFO("Connection manager started on TCP:{}, UDP:{}", tcp_port, udp_port);
    return true;
}

void ConnectionManager::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping connection manager");
    running_ = false;
    
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }
    
    disconnect_all();
    
    if (discovery_) {
        discovery_->stop();
        discovery_.reset();
    }
    
    if (network_manager_) {
        network_manager_->stop_server();
        network_manager_.reset();
    }
}

void ConnectionManager::set_local_info(std::uint32_t peer_id, const std::string& peer_name) {
    local_peer_id_ = peer_id;
    local_peer_name_ = peer_name;
    
    if (discovery_) {
        discovery_->announce_self(peer_id, local_tcp_port_, peer_name);
    }
    
    LOG_INFO("Updated local peer info: ID={}, name='{}'", peer_id, peer_name);
}

bool ConnectionManager::connect_to_peer(const std::string& host, std::uint16_t port) {
    std::string endpoint = host + ":" + std::to_string(port);
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connecting_endpoints_.count(endpoint)) {
            LOG_DEBUG("Already connecting to {}", endpoint);
            return false;
        }
        connecting_endpoints_.insert(endpoint);
    }
    
    LOG_INFO("Connecting to peer at {}:{}", host, port);
    
    auto client = network_manager_->connect_to_peer(host, port);
    if (!client) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connecting_endpoints_.erase(endpoint);
        return false;
    }
    
    return true;
}

void ConnectionManager::disconnect_from_peer(std::uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = peer_connections_.find(peer_id);
    if (it != peer_connections_.end()) {
        auto connection = it->second;
        LOG_INFO("Disconnecting from peer {}", peer_id);
        
        connection->close();
        peer_connections_.erase(it);
        
        auto conn_it = connections_.find(connection);
        if (conn_it != connections_.end()) {
            connections_.erase(conn_it);
        }
    }
}

void ConnectionManager::disconnect_all() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    LOG_INFO("Disconnecting from all peers ({} connections)", connections_.size());
    
    for (auto& [connection, info] : connections_) {
        connection->close();
    }
    
    connections_.clear();
    peer_connections_.clear();
    connecting_endpoints_.clear();
}

std::vector<ConnectionInfo> ConnectionManager::get_connections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    std::vector<ConnectionInfo> result;
    result.reserve(connections_.size());
    
    for (const auto& [connection, info] : connections_) {
        if (info.handshake_state == HandshakeState::COMPLETED) {
            result.push_back(info);
        }
    }
    
    return result;
}

std::optional<ConnectionInfo> ConnectionManager::get_connection_info(std::uint32_t peer_id) const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = peer_connections_.find(peer_id);
    if (it != peer_connections_.end()) {
        auto conn_it = connections_.find(it->second);
        if (conn_it != connections_.end()) {
            return conn_it->second;
        }
    }
    
    return std::nullopt;
}

std::size_t ConnectionManager::get_connection_count() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    std::size_t count = 0;
    for (const auto& [connection, info] : connections_) {
        if (info.handshake_state == HandshakeState::COMPLETED) {
            count++;
        }
    }
    
    return count;
}

void ConnectionManager::handle_new_connection(std::shared_ptr<Connection> connection) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    ConnectionInfo info{
        connection,
        HandshakeState::NONE,
        std::chrono::steady_clock::now(),
        std::chrono::steady_clock::now(),
        0,
        "",
        0,
        false
    };
    
    connections_[connection] = info;
    
    LOG_DEBUG("New connection added: {}", connection->get_remote_endpoint());
    
    // Remove from connecting set if it exists
    std::string endpoint = connection->get_remote_endpoint();
    connecting_endpoints_.erase(endpoint);
}

void ConnectionManager::handle_peer_discovered(const PeerInfo& peer) {
    if (peer.peer_id == local_peer_id_) {
        return; // Don't connect to ourselves
    }
    
    LOG_INFO("Discovered peer: {} at {}:{}", peer.peer_id, peer.ip_address, peer.tcp_port);
    
    // Check if we're already connected to this peer
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (peer_connections_.count(peer.peer_id)) {
            LOG_DEBUG("Already connected to peer {}", peer.peer_id);
            return;
        }
    }
    
    // Attempt to connect
    connect_to_peer(peer.ip_address, peer.tcp_port);
}

void ConnectionManager::handle_peer_lost(std::uint32_t peer_id) {
    LOG_INFO("Peer {} lost from discovery", peer_id);
    // Note: We don't automatically disconnect here as the TCP connection might still be valid
}

void ConnectionManager::handle_handshake(std::shared_ptr<Connection> connection, const HandshakeMessage& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    LOG_INFO("Received handshake from peer {} ({})", msg.peer_id, msg.peer_name);
    
    auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second.peer_id = msg.peer_id;
        it->second.peer_name = msg.peer_name;
        it->second.capabilities = msg.capabilities;
        it->second.handshake_state = HandshakeState::RECEIVED;
        
        connection->set_peer_id(msg.peer_id);
        peer_connections_[msg.peer_id] = connection;
        
        // Send handshake response
        HandshakeMessage response{
            local_peer_id_,
            local_tcp_port_,
            local_peer_name_,
            0 // TODO: Define capability flags
        };
        
        connection->send_message(MessageType::HANDSHAKE_ACK, response);
        it->second.handshake_state = HandshakeState::COMPLETED;
        
        LOG_INFO("Handshake completed with peer {} ({})", msg.peer_id, msg.peer_name);
    }
}

void ConnectionManager::handle_handshake_ack(std::shared_ptr<Connection> connection, const HandshakeMessage& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    LOG_INFO("Received handshake ACK from peer {} ({})", msg.peer_id, msg.peer_name);
    
    auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second.peer_id = msg.peer_id;
        it->second.peer_name = msg.peer_name;
        it->second.capabilities = msg.capabilities;
        it->second.handshake_state = HandshakeState::COMPLETED;
        
        connection->set_peer_id(msg.peer_id);
        peer_connections_[msg.peer_id] = connection;
        
        LOG_INFO("Handshake completed with peer {} ({})", msg.peer_id, msg.peer_name);
    }
}

void ConnectionManager::handle_heartbeat(std::shared_ptr<Connection> connection, const HeartbeatMessage& msg) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second.last_heartbeat = std::chrono::steady_clock::now();
        LOG_DEBUG("Received heartbeat from peer {}", it->second.peer_id);
    }
}

void ConnectionManager::send_handshake(std::shared_ptr<Connection> connection) {
    HandshakeMessage handshake{
        local_peer_id_,
        local_tcp_port_,
        local_peer_name_,
        0 // TODO: Define capability flags
    };
    
    connection->send_message(MessageType::HANDSHAKE, handshake);
    
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(connection);
    if (it != connections_.end()) {
        it->second.handshake_state = HandshakeState::SENT;
    }
    
    LOG_DEBUG("Sent handshake to {}", connection->get_remote_endpoint());
}

void ConnectionManager::send_heartbeat(std::shared_ptr<Connection> connection) {
    HeartbeatMessage heartbeat{
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()),
        static_cast<std::uint32_t>(get_connection_count()),
        0 // TODO: Get actual file count
    };
    
    connection->send_message(MessageType::HEARTBEAT, heartbeat);
}

void ConnectionManager::check_connection_health() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [connection, info] : connections_) {
        if (info.handshake_state == HandshakeState::COMPLETED) {
            // Send heartbeat if needed
            if (now - info.last_heartbeat >= heartbeat_interval_) {
                send_heartbeat(connection);
            }
            
            // Check for timeout
            if (now - info.last_heartbeat > connection_timeout_) {
                LOG_WARN("Connection to peer {} timed out", info.peer_id);
                connection->close();
            }
        } else if (info.handshake_state == HandshakeState::NONE && 
                   now - info.connected_at > std::chrono::seconds(1)) {
            // Send initial handshake
            send_handshake(connection);
        } else if (info.handshake_state == HandshakeState::SENT &&
                   now - info.connected_at > handshake_timeout_) {
            LOG_WARN("Handshake timeout with {}", connection->get_remote_endpoint());
            connection->close();
        }
    }
}

void ConnectionManager::cleanup_failed_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (it->first->get_state() == ConnectionState::DISCONNECTED) {
            LOG_DEBUG("Cleaning up disconnected connection: {}", it->first->get_remote_endpoint());
            
            if (it->second.peer_id != 0) {
                peer_connections_.erase(it->second.peer_id);
            }
            
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

}