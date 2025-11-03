#include "hypershare/network/peer_router.hpp"
#include "hypershare/core/logger.hpp"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

namespace hypershare::network {

namespace {
    constexpr std::size_t MAX_ROUTING_ENTRIES = 10000;
    constexpr std::size_t MAX_FILE_LOCATIONS = 50000;
    constexpr std::size_t MAX_FLOODING_TARGETS = 5;
    constexpr double RELIABILITY_DECAY_FACTOR = 0.95;
    constexpr double BANDWIDTH_WEIGHT = 0.4;
    constexpr double RELIABILITY_WEIGHT = 0.4;
    constexpr double HOP_COUNT_WEIGHT = 0.2;
    
    std::uint32_t calculate_crc32(const std::vector<std::uint8_t>& data) {
        std::uint32_t crc = 0xFFFFFFFF;
        constexpr std::uint32_t polynomial = 0xEDB88320;
        
        for (std::uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ polynomial;
                } else {
                    crc >>= 1;
                }
            }
        }
        return ~crc;
    }
    
    void write_uint32(std::vector<std::uint8_t>& data, std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>(value & 0xFF));
        data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }
    
    std::uint32_t read_uint32(std::span<const std::uint8_t> data, std::size_t& offset) {
        if (offset + 4 > data.size()) {
            throw std::runtime_error("Insufficient data for uint32");
        }
        std::uint32_t value = data[offset] | 
                             (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
                             (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
                             (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return value;
    }
    
    void write_string(std::vector<std::uint8_t>& data, const std::string& str) {
        write_uint32(data, static_cast<std::uint32_t>(str.size()));
        data.insert(data.end(), str.begin(), str.end());
    }
    
    std::string read_string(std::span<const std::uint8_t> data, std::size_t& offset) {
        std::uint32_t size = read_uint32(data, offset);
        if (offset + size > data.size()) {
            throw std::runtime_error("Insufficient data for string");
        }
        std::string result(reinterpret_cast<const char*>(data.data() + offset), size);
        offset += size;
        return result;
    }
}

// RouteUpdateMessage serialization
std::vector<std::uint8_t> RouteUpdateMessage::serialize() const {
    std::vector<std::uint8_t> data;
    
    write_uint32(data, source_peer_id);
    write_uint32(data, static_cast<std::uint32_t>(peer_updates.size()));
    write_uint32(data, static_cast<std::uint32_t>(sequence_number >> 32));
    write_uint32(data, static_cast<std::uint32_t>(sequence_number & 0xFFFFFFFF));
    data.push_back(hop_count);
    
    for (const auto& peer : peer_updates) {
        write_uint32(data, peer.peer_id);
        write_string(data, peer.ip_address);
        data.push_back(static_cast<std::uint8_t>(peer.port >> 8));
        data.push_back(static_cast<std::uint8_t>(peer.port & 0xFF));
        
        auto time_since_epoch = peer.last_seen.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
        write_uint32(data, static_cast<std::uint32_t>(ms >> 32));
        write_uint32(data, static_cast<std::uint32_t>(ms & 0xFFFFFFFF));
        
        data.push_back(peer.hop_count);
        write_uint32(data, peer.next_hop_peer_id);
        
        // Pack reliability score as fixed-point
        std::uint32_t reliability_fp = static_cast<std::uint32_t>(peer.reliability_score * 1000000);
        write_uint32(data, reliability_fp);
        
        write_uint32(data, static_cast<std::uint32_t>(peer.bandwidth_estimate >> 32));
        write_uint32(data, static_cast<std::uint32_t>(peer.bandwidth_estimate & 0xFFFFFFFF));
    }
    
    return data;
}

RouteUpdateMessage RouteUpdateMessage::deserialize(std::span<const std::uint8_t> data) {
    RouteUpdateMessage msg;
    std::size_t offset = 0;
    
    msg.source_peer_id = read_uint32(data, offset);
    std::uint32_t peer_count = read_uint32(data, offset);
    
    std::uint32_t seq_high = read_uint32(data, offset);
    std::uint32_t seq_low = read_uint32(data, offset);
    msg.sequence_number = (static_cast<std::uint64_t>(seq_high) << 32) | seq_low;
    
    if (offset >= data.size()) {
        throw std::runtime_error("Insufficient data for hop_count");
    }
    msg.hop_count = data[offset++];
    
    msg.peer_updates.reserve(peer_count);
    for (std::uint32_t i = 0; i < peer_count; ++i) {
        RoutingPeerInfo peer;
        peer.peer_id = read_uint32(data, offset);
        peer.ip_address = read_string(data, offset);
        
        if (offset + 2 > data.size()) {
            throw std::runtime_error("Insufficient data for port");
        }
        peer.port = (static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;
        
        std::uint32_t time_high = read_uint32(data, offset);
        std::uint32_t time_low = read_uint32(data, offset);
        std::uint64_t ms = (static_cast<std::uint64_t>(time_high) << 32) | time_low;
        peer.last_seen = std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms));
        
        if (offset >= data.size()) {
            throw std::runtime_error("Insufficient data for peer hop_count");
        }
        peer.hop_count = data[offset++];
        
        peer.next_hop_peer_id = read_uint32(data, offset);
        
        std::uint32_t reliability_fp = read_uint32(data, offset);
        peer.reliability_score = static_cast<double>(reliability_fp) / 1000000.0;
        
        std::uint32_t bw_high = read_uint32(data, offset);
        std::uint32_t bw_low = read_uint32(data, offset);
        peer.bandwidth_estimate = (static_cast<std::uint64_t>(bw_high) << 32) | bw_low;
        
        msg.peer_updates.push_back(peer);
    }
    
    return msg;
}

// TopologySyncMessage serialization
std::vector<std::uint8_t> TopologySyncMessage::serialize() const {
    std::vector<std::uint8_t> data;
    
    write_uint32(data, requesting_peer_id);
    write_uint32(data, static_cast<std::uint32_t>(last_known_sequence >> 32));
    write_uint32(data, static_cast<std::uint32_t>(last_known_sequence & 0xFFFFFFFF));
    write_uint32(data, static_cast<std::uint32_t>(known_peers.size()));
    
    for (std::uint32_t peer_id : known_peers) {
        write_uint32(data, peer_id);
    }
    
    return data;
}

TopologySyncMessage TopologySyncMessage::deserialize(std::span<const std::uint8_t> data) {
    TopologySyncMessage msg;
    std::size_t offset = 0;
    
    msg.requesting_peer_id = read_uint32(data, offset);
    
    std::uint32_t seq_high = read_uint32(data, offset);
    std::uint32_t seq_low = read_uint32(data, offset);
    msg.last_known_sequence = (static_cast<std::uint64_t>(seq_high) << 32) | seq_low;
    
    std::uint32_t peer_count = read_uint32(data, offset);
    
    msg.known_peers.reserve(peer_count);
    for (std::uint32_t i = 0; i < peer_count; ++i) {
        msg.known_peers.push_back(read_uint32(data, offset));
    }
    
    return msg;
}

// FileQueryMessage serialization
std::vector<std::uint8_t> FileQueryMessage::serialize() const {
    std::vector<std::uint8_t> data;
    
    write_string(data, file_id);
    write_string(data, query_hash);
    write_uint32(data, source_peer_id);
    write_uint32(data, query_id);
    data.push_back(hop_count);
    
    write_uint32(data, static_cast<std::uint32_t>(search_terms.size()));
    for (const auto& term : search_terms) {
        write_string(data, term);
    }
    
    return data;
}

FileQueryMessage FileQueryMessage::deserialize(std::span<const std::uint8_t> data) {
    FileQueryMessage msg;
    std::size_t offset = 0;
    
    msg.file_id = read_string(data, offset);
    msg.query_hash = read_string(data, offset);
    msg.source_peer_id = read_uint32(data, offset);
    msg.query_id = read_uint32(data, offset);
    
    if (offset >= data.size()) {
        throw std::runtime_error("Insufficient data for hop_count");
    }
    msg.hop_count = data[offset++];
    
    std::uint32_t term_count = read_uint32(data, offset);
    msg.search_terms.reserve(term_count);
    for (std::uint32_t i = 0; i < term_count; ++i) {
        msg.search_terms.push_back(read_string(data, offset));
    }
    
    return msg;
}

// FileQueryResponseMessage serialization
std::vector<std::uint8_t> FileQueryResponseMessage::serialize() const {
    std::vector<std::uint8_t> data;
    
    write_uint32(data, query_id);
    write_uint32(data, responding_peer_id);
    write_uint32(data, static_cast<std::uint32_t>(file_locations.size()));
    
    for (const auto& location : file_locations) {
        write_string(data, location.file_id);
        write_uint32(data, location.peer_id);
        write_string(data, location.file_hash);
        
        write_uint32(data, static_cast<std::uint32_t>(location.file_size >> 32));
        write_uint32(data, static_cast<std::uint32_t>(location.file_size & 0xFFFFFFFF));
        
        auto time_since_epoch = location.announced_at.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
        write_uint32(data, static_cast<std::uint32_t>(ms >> 32));
        write_uint32(data, static_cast<std::uint32_t>(ms & 0xFFFFFFFF));
        
        std::uint32_t availability_fp = static_cast<std::uint32_t>(location.availability_score * 1000000);
        write_uint32(data, availability_fp);
    }
    
    return data;
}

FileQueryResponseMessage FileQueryResponseMessage::deserialize(std::span<const std::uint8_t> data) {
    FileQueryResponseMessage msg;
    std::size_t offset = 0;
    
    msg.query_id = read_uint32(data, offset);
    msg.responding_peer_id = read_uint32(data, offset);
    std::uint32_t location_count = read_uint32(data, offset);
    
    msg.file_locations.reserve(location_count);
    for (std::uint32_t i = 0; i < location_count; ++i) {
        FileLocation location;
        location.file_id = read_string(data, offset);
        location.peer_id = read_uint32(data, offset);
        location.file_hash = read_string(data, offset);
        
        std::uint32_t size_high = read_uint32(data, offset);
        std::uint32_t size_low = read_uint32(data, offset);
        location.file_size = (static_cast<std::uint64_t>(size_high) << 32) | size_low;
        
        std::uint32_t time_high = read_uint32(data, offset);
        std::uint32_t time_low = read_uint32(data, offset);
        std::uint64_t ms = (static_cast<std::uint64_t>(time_high) << 32) | time_low;
        location.announced_at = std::chrono::steady_clock::time_point(std::chrono::milliseconds(ms));
        
        std::uint32_t availability_fp = read_uint32(data, offset);
        location.availability_score = static_cast<double>(availability_fp) / 1000000.0;
        
        msg.file_locations.push_back(location);
    }
    
    return msg;
}

// PeerRouter implementation
PeerRouter::PeerRouter(std::uint32_t local_peer_id)
    : local_peer_id_(local_peer_id)
    , running_(false)
    , route_sequence_number_(0)
{
    LOG_INFO("PeerRouter created for peer {}", local_peer_id_);
}

PeerRouter::~PeerRouter() {
    stop();
}

void PeerRouter::start() {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    if (running_) {
        return;
    }
    
    running_ = true;
    maintenance_thread_ = std::thread(&PeerRouter::routing_maintenance_loop, this);
    
    LOG_INFO("PeerRouter started for peer {}", local_peer_id_);
}

void PeerRouter::stop() {
    {
        std::lock_guard<std::mutex> lock(routing_mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    
    if (maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }
    
    LOG_INFO("PeerRouter stopped for peer {}", local_peer_id_);
}

void PeerRouter::add_direct_peer(std::uint32_t peer_id, const std::string& ip, 
                                std::uint16_t port, std::shared_ptr<Connection> connection) {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    RoutingPeerInfo peer_info;
    peer_info.peer_id = peer_id;
    peer_info.ip_address = ip;
    peer_info.port = port;
    peer_info.last_seen = std::chrono::steady_clock::now();
    peer_info.hop_count = 1;
    peer_info.next_hop_peer_id = peer_id;
    peer_info.reliability_score = 1.0;
    peer_info.bandwidth_estimate = 1000000; // Default 1MB/s
    
    known_peers_[peer_id] = peer_info;
    direct_connections_[peer_id] = connection;
    
    // Add direct route
    RouteEntry route;
    route.destination_peer_id = peer_id;
    route.next_hop_peer_id = peer_id;
    route.hop_count = 1;
    route.last_updated = std::chrono::steady_clock::now();
    route.metric = calculate_route_metric(peer_info);
    
    routing_table_[peer_id] = route;
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_peers = known_peers_.size();
        stats_.direct_peers = direct_connections_.size();
        stats_.route_entries = routing_table_.size();
    }
    
    LOG_INFO("Added direct peer {} at {}:{}", peer_id, ip, port);
}

void PeerRouter::remove_peer(std::uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    known_peers_.erase(peer_id);
    direct_connections_.erase(peer_id);
    routing_table_.erase(peer_id);
    
    // Remove routes that use this peer as next hop
    auto it = routing_table_.begin();
    while (it != routing_table_.end()) {
        if (it->second.next_hop_peer_id == peer_id) {
            it = routing_table_.erase(it);
        } else {
            ++it;
        }
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_peers = known_peers_.size();
        stats_.direct_peers = direct_connections_.size();
        stats_.route_entries = routing_table_.size();
    }
    
    LOG_INFO("Removed peer {}", peer_id);
}

void PeerRouter::announce_file(const std::string& file_id, const std::string& file_hash, 
                              std::uint64_t file_size) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    local_files_.insert(file_id);
    
    FileLocation location;
    location.file_id = file_id;
    location.peer_id = local_peer_id_;
    location.file_hash = file_hash;
    location.file_size = file_size;
    location.announced_at = std::chrono::steady_clock::now();
    location.availability_score = 1.0;
    
    file_locations_[file_id].push_back(location);
    
    // Broadcast file announcement to direct peers
    if (broadcast_sender_) {
        FileAnnounceMessage announce;
        announce.file_id = file_id;
        announce.filename = file_id; // TODO: separate filename from file_id
        announce.file_size = file_size;
        announce.file_hash = file_hash;
        
        broadcast_sender_(MessageType::FILE_ANNOUNCE, announce.serialize());
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.known_files = file_locations_.size();
    }
    
    LOG_INFO("Announced file {} (hash: {}, size: {})", file_id, file_hash, file_size);
}

void PeerRouter::remove_file(const std::string& file_id) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    local_files_.erase(file_id);
    
    auto it = file_locations_.find(file_id);
    if (it != file_locations_.end()) {
        auto& locations = it->second;
        locations.erase(
            std::remove_if(locations.begin(), locations.end(),
                [this](const FileLocation& loc) { return loc.peer_id == local_peer_id_; }),
            locations.end());
        
        if (locations.empty()) {
            file_locations_.erase(it);
        }
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.known_files = file_locations_.size();
    }
    
    LOG_INFO("Removed file {}", file_id);
}

std::vector<FileLocation> PeerRouter::find_file(const std::string& file_id, 
                                               const std::vector<std::string>& search_terms) {
    // Check local file cache first
    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        auto it = file_locations_.find(file_id);
        if (it != file_locations_.end() && !it->second.empty()) {
            LOG_DEBUG("Found file {} in local cache with {} locations", file_id, it->second.size());
            return it->second;
        }
    }
    
    // Generate query hash for deduplication
    std::stringstream ss;
    ss << file_id;
    for (const auto& term : search_terms) {
        ss << "|" << term;
    }
    std::string query_string = ss.str();
    std::uint32_t query_hash_crc = calculate_crc32(
        std::vector<std::uint8_t>(query_string.begin(), query_string.end()));
    
    // Create and broadcast file query
    FileQueryMessage query;
    query.file_id = file_id;
    query.query_hash = std::to_string(query_hash_crc);
    query.source_peer_id = local_peer_id_;
    query.query_id = query_hash_crc;
    query.hop_count = 0;
    query.search_terms = search_terms;
    
    // Cache the query to prevent loops
    {
        std::lock_guard<std::mutex> lock(routing_mutex_);
        query_cache_[query.query_id] = std::chrono::steady_clock::now();
    }
    
    if (broadcast_sender_) {
        // Use ROUTE_UPDATE message type as proxy for file query flooding
        broadcast_sender_(MessageType::ROUTE_UPDATE, query.serialize());
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.queries_processed++;
    }
    
    LOG_INFO("Initiated file query for {} with {} search terms", file_id, search_terms.size());
    
    // Return empty result for now - responses will come via message handlers
    return {};
}

std::optional<std::uint32_t> PeerRouter::get_next_hop(std::uint32_t destination_peer_id) const {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    auto it = routing_table_.find(destination_peer_id);
    if (it != routing_table_.end() && !it->second.is_expired()) {
        return it->second.next_hop_peer_id;
    }
    
    return std::nullopt;
}

std::vector<std::uint32_t> PeerRouter::get_optimal_peers_for_file(const std::string& file_id, 
                                                                 std::size_t max_peers) const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    auto it = file_locations_.find(file_id);
    if (it == file_locations_.end()) {
        return {};
    }
    
    std::vector<std::pair<std::uint32_t, double>> peer_scores;
    
    for (const auto& location : it->second) {
        double score = location.availability_score;
        
        // Boost score for closer peers
        {
            std::lock_guard<std::mutex> route_lock(routing_mutex_);
            auto route_it = routing_table_.find(location.peer_id);
            if (route_it != routing_table_.end()) {
                score /= (1.0 + route_it->second.hop_count);
            }
        }
        
        peer_scores.emplace_back(location.peer_id, score);
    }
    
    // Sort by score descending
    std::sort(peer_scores.begin(), peer_scores.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<std::uint32_t> result;
    for (std::size_t i = 0; i < std::min(max_peers, peer_scores.size()); ++i) {
        result.push_back(peer_scores[i].first);
    }
    
    return result;
}

bool PeerRouter::forward_message(std::uint32_t destination_peer_id, MessageType type, 
                                const std::vector<std::uint8_t>& payload) {
    auto next_hop = get_next_hop(destination_peer_id);
    if (!next_hop) {
        LOG_WARN("No route to peer {}", destination_peer_id);
        return false;
    }
    
    if (message_sender_) {
        message_sender_(*next_hop, type, payload);
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.messages_forwarded++;
        return true;
    }
    
    return false;
}

void PeerRouter::handle_route_update(std::shared_ptr<Connection> connection, 
                                    const RouteUpdateMessage& message) {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    // Ignore our own updates
    if (message.source_peer_id == local_peer_id_) {
        return;
    }
    
    // Prevent routing loops by checking hop count
    if (message.hop_count >= MAX_HOP_COUNT) {
        LOG_WARN("Dropping route update due to hop count limit");
        return;
    }
    
    bool routing_changed = false;
    
    for (const auto& peer_update : message.peer_updates) {
        // Skip our own peer info
        if (peer_update.peer_id == local_peer_id_) {
            continue;
        }
        
        std::uint8_t new_hop_count = peer_update.hop_count + 1;
        if (new_hop_count >= MAX_HOP_COUNT) {
            continue;
        }
        
        auto existing_peer = known_peers_.find(peer_update.peer_id);
        bool should_update = false;
        
        if (existing_peer == known_peers_.end()) {
            should_update = true;
        } else {
            // Update if we found a better route (lower hop count or better metric)
            auto existing_route = routing_table_.find(peer_update.peer_id);
            if (existing_route != routing_table_.end()) {
                double new_metric = calculate_route_metric(peer_update);
                if (new_hop_count < existing_route->second.hop_count ||
                    (new_hop_count == existing_route->second.hop_count && new_metric < existing_route->second.metric)) {
                    should_update = true;
                }
            }
        }
        
        if (should_update && routing_table_.size() < MAX_ROUTING_ENTRIES) {
            RoutingPeerInfo new_peer = peer_update;
            new_peer.hop_count = new_hop_count;
            new_peer.next_hop_peer_id = message.source_peer_id;
            
            known_peers_[peer_update.peer_id] = new_peer;
            
            RouteEntry route;
            route.destination_peer_id = peer_update.peer_id;
            route.next_hop_peer_id = message.source_peer_id;
            route.hop_count = new_hop_count;
            route.last_updated = std::chrono::steady_clock::now();
            route.metric = calculate_route_metric(new_peer);
            
            routing_table_[peer_update.peer_id] = route;
            routing_changed = true;
            
            LOG_DEBUG("Updated route to peer {} via {} (hop count: {})", 
                     peer_update.peer_id, message.source_peer_id, new_hop_count);
        }
    }
    
    if (routing_changed) {
        route_sequence_number_++;
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_peers = known_peers_.size();
        stats_.route_entries = routing_table_.size();
        
        // Calculate average hop count
        double total_hops = 0.0;
        for (const auto& [id, route] : routing_table_) {
            total_hops += route.hop_count;
        }
        stats_.average_hop_count = routing_table_.empty() ? 0.0 : total_hops / routing_table_.size();
    }
}

void PeerRouter::handle_topology_sync(std::shared_ptr<Connection> connection, 
                                     const TopologySyncMessage& message) {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    // Respond with our routing updates
    RouteUpdateMessage response;
    response.source_peer_id = local_peer_id_;
    response.sequence_number = route_sequence_number_;
    response.hop_count = 0;
    
    for (const auto& [peer_id, peer_info] : known_peers_) {
        if (peer_id != message.requesting_peer_id) {
            response.peer_updates.push_back(peer_info);
        }
    }
    
    if (message_sender_) {
        message_sender_(message.requesting_peer_id, MessageType::ROUTE_UPDATE, response.serialize());
    }
    
    LOG_DEBUG("Responded to topology sync from peer {}", message.requesting_peer_id);
}

void PeerRouter::handle_file_query(std::shared_ptr<Connection> connection, 
                                  const FileQueryMessage& message) {
    // Check if we've already seen this query
    {
        std::lock_guard<std::mutex> lock(routing_mutex_);
        auto cached_query = query_cache_.find(message.query_id);
        if (cached_query != query_cache_.end()) {
            // Check if query is recent (within last 60 seconds)
            auto age = std::chrono::steady_clock::now() - cached_query->second;
            if (age < std::chrono::seconds(60)) {
                LOG_DEBUG("Ignoring duplicate query {}", message.query_id);
                return;
            }
        }
        
        query_cache_[message.query_id] = std::chrono::steady_clock::now();
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.queries_processed++;
    }
    
    // Check if we have the file
    std::vector<FileLocation> matching_locations;
    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        auto it = file_locations_.find(message.file_id);
        if (it != file_locations_.end()) {
            for (const auto& location : it->second) {
                if (location.peer_id == local_peer_id_) {
                    matching_locations.push_back(location);
                }
            }
        }
    }
    
    // If we have the file, respond directly
    if (!matching_locations.empty()) {
        FileQueryResponseMessage response;
        response.query_id = message.query_id;
        response.responding_peer_id = local_peer_id_;
        response.file_locations = matching_locations;
        
        if (message_sender_) {
            message_sender_(message.source_peer_id, MessageType::ROUTE_UPDATE, response.serialize());
        }
        
        LOG_INFO("Responded to file query {} with {} locations", message.query_id, matching_locations.size());
        return;
    }
    
    // Forward the query if hop count allows
    if (message.hop_count < MAX_HOP_COUNT - 1) {
        FileQueryMessage forwarded = message;
        forwarded.hop_count++;
        
        auto flooding_targets = get_flooding_targets(message.source_peer_id, message.hop_count);
        for (std::uint32_t target_peer : flooding_targets) {
            if (message_sender_) {
                message_sender_(target_peer, MessageType::ROUTE_UPDATE, forwarded.serialize());
            }
        }
        
        LOG_DEBUG("Forwarded file query {} to {} peers", message.query_id, flooding_targets.size());
    }
}

void PeerRouter::handle_file_query_response(std::shared_ptr<Connection> connection, 
                                           const FileQueryResponseMessage& message) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    // Store the file locations
    for (const auto& location : message.file_locations) {
        auto& locations = file_locations_[location.file_id];
        
        // Check if we already have this location
        auto existing = std::find_if(locations.begin(), locations.end(),
            [&location](const FileLocation& loc) {
                return loc.peer_id == location.peer_id && loc.file_hash == location.file_hash;
            });
        
        if (existing == locations.end() && locations.size() < MAX_FILE_LOCATIONS) {
            locations.push_back(location);
            LOG_DEBUG("Added file location for {} from peer {}", 
                     location.file_id, location.peer_id);
        }
    }
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.known_files = file_locations_.size();
    }
}

std::vector<RoutingPeerInfo> PeerRouter::get_known_peers() const {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    std::vector<RoutingPeerInfo> peers;
    peers.reserve(known_peers_.size());
    
    for (const auto& [id, peer] : known_peers_) {
        peers.push_back(peer);
    }
    
    return peers;
}

std::vector<RouteEntry> PeerRouter::get_routing_table() const {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    std::vector<RouteEntry> routes;
    routes.reserve(routing_table_.size());
    
    for (const auto& [id, route] : routing_table_) {
        routes.push_back(route);
    }
    
    return routes;
}

std::vector<FileLocation> PeerRouter::get_file_locations(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    std::vector<FileLocation> locations;
    
    if (file_id.empty()) {
        // Return all file locations
        for (const auto& [id, file_locs] : file_locations_) {
            locations.insert(locations.end(), file_locs.begin(), file_locs.end());
        }
    } else {
        auto it = file_locations_.find(file_id);
        if (it != file_locations_.end()) {
            locations = it->second;
        }
    }
    
    return locations;
}

void PeerRouter::set_message_sender(std::function<void(std::uint32_t, MessageType, const std::vector<std::uint8_t>&)> sender) {
    message_sender_ = std::move(sender);
}

void PeerRouter::set_broadcast_sender(std::function<void(MessageType, const std::vector<std::uint8_t>&)> sender) {
    broadcast_sender_ = std::move(sender);
}

PeerRouter::Statistics PeerRouter::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void PeerRouter::routing_maintenance_loop() {
    while (running_) {
        try {
            cleanup_expired_entries();
            send_route_updates();
            
            // Send topology sync less frequently
            static int cycle_count = 0;
            if (++cycle_count % 5 == 0) {
                send_topology_sync();
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error in routing maintenance: {}", e.what());
        }
        
        std::this_thread::sleep_for(TOPOLOGY_UPDATE_INTERVAL);
    }
}

void PeerRouter::send_route_updates() {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    if (known_peers_.empty() || !broadcast_sender_) {
        return;
    }
    
    RouteUpdateMessage update;
    update.source_peer_id = local_peer_id_;
    update.sequence_number = ++route_sequence_number_;
    update.hop_count = 0;
    
    for (const auto& [peer_id, peer_info] : known_peers_) {
        update.peer_updates.push_back(peer_info);
    }
    
    broadcast_sender_(MessageType::ROUTE_UPDATE, update.serialize());
    
    LOG_DEBUG("Sent route update with {} peers (seq: {})", 
             update.peer_updates.size(), update.sequence_number);
}

void PeerRouter::send_topology_sync() {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    if (known_peers_.empty() || !broadcast_sender_) {
        return;
    }
    
    TopologySyncMessage sync;
    sync.requesting_peer_id = local_peer_id_;
    sync.last_known_sequence = route_sequence_number_;
    
    for (const auto& [peer_id, _] : known_peers_) {
        sync.known_peers.push_back(peer_id);
    }
    
    broadcast_sender_(MessageType::TOPOLOGY_SYNC, sync.serialize());
    
    LOG_DEBUG("Sent topology sync for {} known peers", sync.known_peers.size());
}

void PeerRouter::cleanup_expired_entries() {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    // Clean up expired peers
    auto peer_it = known_peers_.begin();
    while (peer_it != known_peers_.end()) {
        if (peer_it->second.is_expired()) {
            LOG_DEBUG("Removing expired peer {}", peer_it->first);
            routing_table_.erase(peer_it->first);
            peer_it = known_peers_.erase(peer_it);
        } else {
            // Decay reliability scores
            peer_it->second.reliability_score *= RELIABILITY_DECAY_FACTOR;
            ++peer_it;
        }
    }
    
    // Clean up expired routes
    auto route_it = routing_table_.begin();
    while (route_it != routing_table_.end()) {
        if (route_it->second.is_expired()) {
            LOG_DEBUG("Removing expired route to {}", route_it->first);
            route_it = routing_table_.erase(route_it);
        } else {
            ++route_it;
        }
    }
    
    // Clean up expired file locations
    {
        std::lock_guard<std::mutex> file_lock(file_mutex_);
        for (auto& [file_id, locations] : file_locations_) {
            locations.erase(
                std::remove_if(locations.begin(), locations.end(),
                    [](const FileLocation& loc) {
                        auto age = std::chrono::steady_clock::now() - loc.announced_at;
                        return age > std::chrono::hours(1); // Expire after 1 hour
                    }),
                locations.end());
        }
        
        // Remove files with no locations
        auto file_it = file_locations_.begin();
        while (file_it != file_locations_.end()) {
            if (file_it->second.empty()) {
                file_it = file_locations_.erase(file_it);
            } else {
                ++file_it;
            }
        }
    }
    
    // Clean up old queries
    auto query_it = query_cache_.begin();
    while (query_it != query_cache_.end()) {
        auto age = std::chrono::steady_clock::now() - query_it->second;
        if (age > std::chrono::minutes(5)) {
            query_it = query_cache_.erase(query_it);
        } else {
            ++query_it;
        }
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_peers = known_peers_.size();
        stats_.direct_peers = direct_connections_.size();
        stats_.route_entries = routing_table_.size();
        
        std::lock_guard<std::mutex> file_lock(file_mutex_);
        stats_.known_files = file_locations_.size();
    }
}

void PeerRouter::update_peer_reliability(std::uint32_t peer_id, bool success) {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    auto peer_it = known_peers_.find(peer_id);
    if (peer_it != known_peers_.end()) {
        if (success) {
            peer_it->second.reliability_score = std::min(1.0, peer_it->second.reliability_score + 0.1);
        } else {
            peer_it->second.reliability_score = std::max(0.0, peer_it->second.reliability_score - 0.2);
        }
        
        // Update route metric
        auto route_it = routing_table_.find(peer_id);
        if (route_it != routing_table_.end()) {
            route_it->second.metric = calculate_route_metric(peer_it->second);
        }
    }
}

double PeerRouter::calculate_route_metric(const RoutingPeerInfo& peer) const {
    // Lower metric is better
    double metric = static_cast<double>(peer.hop_count) * HOP_COUNT_WEIGHT;
    
    // Penalize unreliable peers
    metric += (1.0 - peer.reliability_score) * RELIABILITY_WEIGHT;
    
    // Penalize low bandwidth peers (normalize by 1MB/s)
    double bandwidth_factor = 1000000.0 / std::max(1000.0, static_cast<double>(peer.bandwidth_estimate));
    metric += bandwidth_factor * BANDWIDTH_WEIGHT;
    
    return metric;
}

void PeerRouter::rebuild_routing_table() {
    std::lock_guard<std::mutex> lock(routing_mutex_);
    
    routing_table_.clear();
    
    // Add direct routes
    for (const auto& [peer_id, peer_info] : known_peers_) {
        if (peer_info.is_direct()) {
            RouteEntry route;
            route.destination_peer_id = peer_id;
            route.next_hop_peer_id = peer_id;
            route.hop_count = 1;
            route.last_updated = std::chrono::steady_clock::now();
            route.metric = calculate_route_metric(peer_info);
            
            routing_table_[peer_id] = route;
        }
    }
    
    // TODO: Implement more sophisticated routing algorithm if needed
    // For now, we rely on distance-vector updates from peers
    
    LOG_DEBUG("Rebuilt routing table with {} entries", routing_table_.size());
}

std::vector<std::uint32_t> PeerRouter::get_flooding_targets(std::uint32_t source_peer_id, 
                                                           std::uint8_t max_hops) const {
    std::vector<std::uint32_t> targets;
    targets.reserve(MAX_FLOODING_TARGETS);
    
    // Select random subset of direct peers (excluding source)
    std::vector<std::uint32_t> candidates;
    for (const auto& [peer_id, connection] : direct_connections_) {
        if (peer_id != source_peer_id) {
            candidates.push_back(peer_id);
        }
    }
    
    if (candidates.empty()) {
        return targets;
    }
    
    // Randomly shuffle and select up to MAX_FLOODING_TARGETS
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);
    
    std::size_t target_count = std::min(MAX_FLOODING_TARGETS, candidates.size());
    targets.assign(candidates.begin(), candidates.begin() + target_count);
    
    return targets;
}

}