#include "hypershare/network/udp_discovery.hpp"
#include "hypershare/core/logger.hpp"
#include <boost/asio/ip/multicast.hpp>

namespace hypershare::network {

UdpDiscovery::UdpDiscovery(std::uint16_t discovery_port)
    : discovery_port_(discovery_port)
    , running_(false)
    , io_context_()
    , socket_(io_context_)
    , multicast_endpoint_(boost::asio::ip::make_address("239.255.42.99"), discovery_port)
    , local_peer_id_(0)
    , local_tcp_port_(0)
    , announcement_interval_(std::chrono::seconds(30))
    , peer_timeout_(std::chrono::minutes(2))
    , last_announcement_(std::chrono::steady_clock::now())
    , last_cleanup_(std::chrono::steady_clock::now()) {
    
    LOG_INFO("UDP discovery initialized on port {}", discovery_port_);
}

UdpDiscovery::~UdpDiscovery() {
    stop();
}

bool UdpDiscovery::start() {
    if (running_) {
        LOG_WARN("UDP discovery already running");
        return false;
    }
    
    try {
        socket_.open(udp::v4());
        socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
        socket_.set_option(boost::asio::ip::multicast::enable_loopback(false));
        socket_.bind(udp::endpoint(udp::v4(), discovery_port_));
        
        socket_.set_option(boost::asio::ip::multicast::join_group(multicast_endpoint_.address()));
        
        running_ = true;
        
        do_receive();
        
        io_thread_ = std::thread([this]() {
            LOG_INFO("UDP discovery IO thread started");
            while (running_) {
                try {
                    io_context_.run();
                    break;
                } catch (const std::exception& e) {
                    LOG_ERROR("UDP discovery IO error: {}", e.what());
                    if (!running_) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    io_context_.restart();
                }
            }
            LOG_INFO("UDP discovery IO thread stopped");
        });
        
        discovery_thread_ = std::thread([this]() {
            discovery_loop();
        });
        
        LOG_INFO("UDP discovery started on {}", multicast_endpoint_.address().to_string());
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start UDP discovery: {}", e.what());
        running_ = false;
        return false;
    }
}

void UdpDiscovery::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping UDP discovery");
    running_ = false;
    
    boost::system::error_code ec;
    socket_.close(ec);
    io_context_.stop();
    
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    discovered_peers_.clear();
}

void UdpDiscovery::announce_self(std::uint32_t peer_id, std::uint16_t tcp_port, const std::string& peer_name) {
    local_peer_id_ = peer_id;
    local_tcp_port_ = tcp_port;
    local_peer_name_ = peer_name;
    
    LOG_INFO("Configured local peer: ID={}, TCP port={}, name='{}'", 
             peer_id, tcp_port, peer_name);
    
    if (running_) {
        send_announcement();
    }
}

void UdpDiscovery::query_peers() {
    if (running_) {
        send_peer_query();
    }
}

std::vector<PeerInfo> UdpDiscovery::get_discovered_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::vector<PeerInfo> peers;
    peers.reserve(discovered_peers_.size());
    
    for (const auto& [id, info] : discovered_peers_) {
        peers.push_back(info);
    }
    
    return peers;
}

std::optional<PeerInfo> UdpDiscovery::get_peer_info(std::uint32_t peer_id) const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = discovered_peers_.find(peer_id);
    if (it != discovered_peers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::size_t UdpDiscovery::get_peer_count() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return discovered_peers_.size();
}

void UdpDiscovery::do_receive() {
    if (!running_) {
        return;
    }
    
    socket_.async_receive_from(
        boost::asio::buffer(receive_buffer_), sender_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_received) {
            if (!ec && running_) {
                std::vector<std::uint8_t> data(receive_buffer_.begin(), 
                                              receive_buffer_.begin() + bytes_received);
                handle_discovery_message(sender_endpoint_, std::move(data));
                do_receive();
            } else if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("UDP receive error: {}", ec.message());
                if (running_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    do_receive();
                }
            }
        });
}

void UdpDiscovery::handle_discovery_message(const boost::asio::ip::udp::endpoint& sender, std::vector<std::uint8_t> data) {
    try {
        if (data.size() < MESSAGE_HEADER_SIZE) {
            return;
        }
        
        std::span<const std::uint8_t> data_span(data);
        auto header = MessageHeader::deserialize(data_span.subspan(0, MESSAGE_HEADER_SIZE));
        
        if (!header.is_valid() || data.size() < MESSAGE_HEADER_SIZE + header.payload_size) {
            return;
        }
        
        std::vector<std::uint8_t> payload(data.begin() + MESSAGE_HEADER_SIZE,
                                         data.begin() + MESSAGE_HEADER_SIZE + header.payload_size);
        
        if (!header.verify_checksum(payload)) {
            LOG_WARN("Discovery message checksum mismatch from {}", sender.address().to_string());
            return;
        }
        
        switch (header.type) {
            case MessageType::PEER_ANNOUNCE: {
                auto msg = PeerAnnounceMessage::deserialize(payload);
                handle_peer_announce(sender, msg);
                break;
            }
            case MessageType::PEER_QUERY:
                handle_peer_query(sender);
                break;
            case MessageType::PEER_RESPONSE: {
                auto msg = PeerAnnounceMessage::deserialize(payload);
                handle_peer_response(sender, msg);
                break;
            }
            default:
                LOG_DEBUG("Ignoring discovery message type {} from {}", 
                          static_cast<int>(header.type), sender.address().to_string());
                break;
        }
        
    } catch (const std::exception& e) {
        LOG_WARN("Failed to process discovery message from {}: {}", 
                 sender.address().to_string(), e.what());
    }
}

void UdpDiscovery::send_announcement() {
    if (!running_ || local_peer_id_ == 0) {
        return;
    }
    
    PeerAnnounceMessage msg{
        local_peer_id_,
        "0.0.0.0", // Will be replaced by receiver with actual IP
        local_tcp_port_,
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count())
    };
    
    auto payload = msg.serialize();
    MessageHeader header(MessageType::PEER_ANNOUNCE, static_cast<std::uint32_t>(payload.size()));
    header.calculate_checksum(payload);
    
    auto header_data = header.serialize();
    std::vector<std::uint8_t> message;
    message.reserve(header_data.size() + payload.size());
    message.insert(message.end(), header_data.begin(), header_data.end());
    message.insert(message.end(), payload.begin(), payload.end());
    
    socket_.async_send_to(
        boost::asio::buffer(message), multicast_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_sent) {
            if (ec) {
                LOG_ERROR("Failed to send announcement: {}", ec.message());
            } else {
                LOG_DEBUG("Sent peer announcement ({} bytes)", bytes_sent);
            }
        });
    
    last_announcement_ = std::chrono::steady_clock::now();
}

void UdpDiscovery::send_peer_query() {
    if (!running_) {
        return;
    }
    
    MessageHeader header(MessageType::PEER_QUERY, 0);
    auto header_data = header.serialize();
    
    socket_.async_send_to(
        boost::asio::buffer(header_data), multicast_endpoint_,
        [this](boost::system::error_code ec, std::size_t bytes_sent) {
            if (ec) {
                LOG_ERROR("Failed to send peer query: {}", ec.message());
            } else {
                LOG_DEBUG("Sent peer query ({} bytes)", bytes_sent);
            }
        });
}

void UdpDiscovery::cleanup_expired_peers() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    auto it = discovered_peers_.begin();
    while (it != discovered_peers_.end()) {
        if (now - it->second.last_seen > peer_timeout_) {
            LOG_INFO("Peer {} ({}) timed out", it->second.peer_id, it->second.ip_address);
            
            if (peer_lost_handler_) {
                peer_lost_handler_(it->second.peer_id);
            }
            
            it = discovered_peers_.erase(it);
        } else {
            ++it;
        }
    }
    
    last_cleanup_ = now;
}

void UdpDiscovery::discovery_loop() {
    LOG_INFO("UDP discovery loop started");
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        
        if (now - last_announcement_ >= announcement_interval_) {
            send_announcement();
        }
        
        if (now - last_cleanup_ >= std::chrono::seconds(60)) {
            cleanup_expired_peers();
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_INFO("UDP discovery loop stopped");
}

void UdpDiscovery::handle_peer_announce(const boost::asio::ip::udp::endpoint& sender, const PeerAnnounceMessage& msg) {
    if (msg.peer_id == local_peer_id_) {
        return; // Ignore our own announcements
    }
    
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    auto it = discovered_peers_.find(msg.peer_id);
    bool is_new_peer = (it == discovered_peers_.end());
    
    PeerInfo info{
        msg.peer_id,
        sender.address().to_string(),
        msg.port,
        "", // We don't have peer name in announce message
        0,  // We don't have capabilities in announce message
        std::chrono::steady_clock::now(),
        false
    };
    
    discovered_peers_[msg.peer_id] = info;
    
    if (is_new_peer) {
        LOG_INFO("Discovered new peer: {} at {}:{}", msg.peer_id, info.ip_address, msg.port);
        
        if (peer_discovered_handler_) {
            peer_discovered_handler_(info);
        }
    } else {
        LOG_DEBUG("Updated peer info: {} at {}:{}", msg.peer_id, info.ip_address, msg.port);
    }
}

void UdpDiscovery::handle_peer_query(const boost::asio::ip::udp::endpoint& sender) {
    if (local_peer_id_ == 0) {
        return;
    }
    
    PeerAnnounceMessage response{
        local_peer_id_,
        "0.0.0.0",
        local_tcp_port_,
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count())
    };
    
    auto payload = response.serialize();
    MessageHeader header(MessageType::PEER_RESPONSE, static_cast<std::uint32_t>(payload.size()));
    header.calculate_checksum(payload);
    
    auto header_data = header.serialize();
    std::vector<std::uint8_t> message;
    message.reserve(header_data.size() + payload.size());
    message.insert(message.end(), header_data.begin(), header_data.end());
    message.insert(message.end(), payload.begin(), payload.end());
    
    socket_.async_send_to(
        boost::asio::buffer(message), sender,
        [this, sender](boost::system::error_code ec, std::size_t bytes_sent) {
            if (ec) {
                LOG_ERROR("Failed to send peer response to {}: {}", 
                          sender.address().to_string(), ec.message());
            } else {
                LOG_DEBUG("Sent peer response to {} ({} bytes)", 
                          sender.address().to_string(), bytes_sent);
            }
        });
}

void UdpDiscovery::handle_peer_response(const boost::asio::ip::udp::endpoint& sender, const PeerAnnounceMessage& msg) {
    handle_peer_announce(sender, msg); // Same handling as announcement
}

}