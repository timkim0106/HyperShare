#include "hypershare/network/network_manager.hpp"
#include "hypershare/core/logger.hpp"
#include <random>

namespace hypershare::network {

NetworkManager::NetworkManager()
    : connection_timeout_(std::chrono::seconds(30))
    , heartbeat_interval_(std::chrono::seconds(30))
    , running_(false) {
    
    std::random_device rd;
    std::mt19937 gen(rd());
    local_peer_id_ = gen();
    
    LOG_INFO("Network manager initialized with peer ID: {}", local_peer_id_);
    
    register_message_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
        [this](std::shared_ptr<Connection> connection, const HeartbeatMessage& msg) {
            LOG_DEBUG("Received heartbeat from {}", connection->get_remote_endpoint());
            // Update last seen time, etc.
        });
    
    register_message_handler<HandshakeMessage>(MessageType::HANDSHAKE,
        [this](std::shared_ptr<Connection> connection, const HandshakeMessage& msg) {
            LOG_INFO("Received handshake from peer {} ({})", msg.peer_id, msg.peer_name);
            connection->set_peer_id(msg.peer_id);
            
            // Send handshake response
            HandshakeMessage response{
                local_peer_id_,
                8080, // TODO: Get actual port from config
                "HyperShare Node",
                0 // TODO: Define capability flags
            };
            connection->send_message(MessageType::HANDSHAKE_ACK, response);
        });
}

NetworkManager::~NetworkManager() {
    running_ = false;
    stop_server();
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

bool NetworkManager::start_server(std::uint16_t port) {
    if (server_ && server_->is_running()) {
        LOG_WARN("Server already running");
        return false;
    }
    
    server_ = std::make_unique<TcpServer>(port);
    
    server_->set_connection_handler(
        [this](std::shared_ptr<Connection> connection) {
            handle_new_connection(connection);
        });
    
    server_->set_message_handler(
        [this](std::shared_ptr<Connection> connection, const MessageHeader& header, std::vector<std::uint8_t> payload) {
            handle_message(connection, header, std::move(payload));
        });
    
    if (server_->start()) {
        running_ = true;
        
        heartbeat_thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(heartbeat_interval_);
                if (running_) {
                    send_heartbeats();
                    cleanup_disconnected_clients();
                }
            }
        });
        
        return true;
    }
    
    return false;
}

void NetworkManager::stop_server() {
    running_ = false;
    
    if (server_) {
        server_->stop();
        server_.reset();
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& [endpoint, client] : clients_) {
        client->disconnect();
    }
    clients_.clear();
}

bool NetworkManager::is_server_running() const {
    return server_ && server_->is_running();
}

std::shared_ptr<TcpClient> NetworkManager::connect_to_peer(const std::string& host, std::uint16_t port) {
    std::string endpoint = host + ":" + std::to_string(port);
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(endpoint);
    if (it != clients_.end() && it->second->is_connected()) {
        LOG_INFO("Already connected to {}", endpoint);
        return it->second;
    }
    
    auto client = std::make_shared<TcpClient>();
    
    client->set_connect_handler(
        [this, client](bool success, const std::string& error) {
            handle_client_connected(client, success);
        });
    
    client->set_message_handler(
        [this](const MessageHeader& header, std::vector<std::uint8_t> payload) {
            // Convert to connection-based handler
            // TODO: Need to associate client with connection
        });
    
    auto future = client->connect_async(host, port);
    clients_[endpoint] = client;
    
    // Send initial handshake after connection
    if (future.wait_for(connection_timeout_) == std::future_status::ready && future.get()) {
        HandshakeMessage handshake{
            local_peer_id_,
            8080, // TODO: Get actual port
            "HyperShare Node",
            0
        };
        client->send_message(MessageType::HANDSHAKE, handshake);
    }
    
    return client;
}

void NetworkManager::disconnect_from_peer(const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(endpoint);
    if (it != clients_.end()) {
        it->second->disconnect();
        clients_.erase(it);
        LOG_INFO("Disconnected from peer {}", endpoint);
    }
}

void NetworkManager::broadcast_message(MessageType type, const std::vector<std::uint8_t>& payload) {
    if (server_) {
        MessageHeader header(type, static_cast<std::uint32_t>(payload.size()));
        header.calculate_checksum(payload);
        server_->broadcast_message(header, payload);
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [endpoint, client] : clients_) {
        if (client->is_connected()) {
            MessageHeader header(type, static_cast<std::uint32_t>(payload.size()));
            header.calculate_checksum(payload);
            client->send_message(header, payload);
        }
    }
    
    LOG_DEBUG("Broadcasted message type {} to all peers", static_cast<int>(type));
}

void NetworkManager::send_to_peer(const std::string& endpoint, MessageType type, const std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(endpoint);
    if (it != clients_.end() && it->second->is_connected()) {
        MessageHeader header(type, static_cast<std::uint32_t>(payload.size()));
        header.calculate_checksum(payload);
        it->second->send_message(header, payload);
        LOG_DEBUG("Sent message type {} to peer {}", static_cast<int>(type), endpoint);
    } else {
        LOG_WARN("Cannot send message to disconnected peer {}", endpoint);
    }
}

std::vector<std::string> NetworkManager::get_connected_peers() const {
    std::vector<std::string> peers;
    
    if (server_) {
        auto connections = server_->get_connections();
        for (const auto& conn : connections) {
            peers.push_back(conn->get_remote_endpoint());
        }
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [endpoint, client] : clients_) {
        if (client->is_connected()) {
            peers.push_back(endpoint);
        }
    }
    
    return peers;
}

std::size_t NetworkManager::get_peer_count() const {
    std::size_t count = 0;
    
    if (server_) {
        count += server_->get_connection_count();
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& [endpoint, client] : clients_) {
        if (client->is_connected()) {
            count++;
        }
    }
    
    return count;
}

void NetworkManager::handle_new_connection(std::shared_ptr<Connection> connection) {
    LOG_INFO("New peer connected: {}", connection->get_remote_endpoint());
}

void NetworkManager::handle_client_connected(std::shared_ptr<TcpClient> client, bool success) {
    if (success) {
        LOG_INFO("Successfully connected to peer: {}", client->get_remote_endpoint());
    } else {
        LOG_ERROR("Failed to connect to peer");
    }
}

void NetworkManager::handle_message(std::shared_ptr<Connection> connection, const MessageHeader& header, std::vector<std::uint8_t> payload) {
    message_handler_.handle_message(connection, header, std::move(payload));
}

void NetworkManager::cleanup_disconnected_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.begin();
    while (it != clients_.end()) {
        if (!it->second->is_connected()) {
            LOG_DEBUG("Removing disconnected client: {}", it->first);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void NetworkManager::send_heartbeats() {
    HeartbeatMessage heartbeat{
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(),
        static_cast<std::uint32_t>(get_peer_count()),
        0 // TODO: Get actual file count
    };
    
    broadcast_message(MessageType::HEARTBEAT, heartbeat);
}

}