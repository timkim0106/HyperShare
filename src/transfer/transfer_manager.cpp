#include "hypershare/transfer/transfer_manager.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace hypershare::transfer {

TransferManager::TransferManager(const hypershare::storage::StorageConfig& config)
    : config_(config)
    , max_concurrent_transfers_(10)
    , global_bandwidth_limit_(0) // 0 means no limit
    , total_bytes_transferred_(0)
    , start_time_(std::chrono::steady_clock::now())
{
}

TransferManager::~TransferManager() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    active_sessions_.clear();
}

std::string TransferManager::start_download(const std::string& file_id, uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    if (!can_start_new_transfer()) {
        return ""; // Cannot start new transfer
    }
    
    auto session_id = generate_session_id();
    auto session = std::make_unique<TransferSession>(session_id, file_id, peer_id);
    
    active_sessions_[session_id] = std::move(session);
    
    return session_id;
}

std::string TransferManager::start_upload(const std::string& file_id, uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    if (!can_start_new_transfer()) {
        return ""; // Cannot start new transfer
    }
    
    auto session_id = generate_session_id();
    auto session = std::make_unique<TransferSession>(session_id, file_id, peer_id);
    
    active_sessions_[session_id] = std::move(session);
    
    return session_id;
}

bool TransferManager::has_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return active_sessions_.find(session_id) != active_sessions_.end();
}

SessionStats TransferManager::get_session_stats(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        return create_session_stats(*it->second);
    }
    
    // Return empty stats if session not found
    return SessionStats{};
}

std::vector<SessionStats> TransferManager::get_all_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    std::vector<SessionStats> all_stats;
    for (const auto& [session_id, session] : active_sessions_) {
        all_stats.push_back(create_session_stats(*session));
    }
    
    return all_stats;
}

hypershare::crypto::CryptoResult TransferManager::pause_transfer(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Session not found"
        );
    }
    
    auto& session = it->second;
    if (session->get_state() == TransferState::TRANSFERRING) {
        session->set_state(TransferState::PAUSED);
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    }
    
    return hypershare::crypto::CryptoResult(
        hypershare::crypto::CryptoError::INVALID_STATE,
        "Cannot pause transfer in current state"
    );
}

hypershare::crypto::CryptoResult TransferManager::resume_transfer(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Session not found"
        );
    }
    
    auto& session = it->second;
    if (session->get_state() == TransferState::PAUSED) {
        session->set_state(TransferState::TRANSFERRING);
        return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
    }
    
    return hypershare::crypto::CryptoResult(
        hypershare::crypto::CryptoError::INVALID_STATE,
        "Cannot resume transfer in current state"
    );
}

hypershare::crypto::CryptoResult TransferManager::cancel_transfer(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Session not found"
        );
    }
    
    // Set state to cancelled and remove from active sessions
    it->second->set_state(TransferState::CANCELLED);
    active_sessions_.erase(it);
    
    return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
}

hypershare::crypto::CryptoResult TransferManager::handle_chunk_request(const std::string& session_id,
                                                                        uint32_t chunk_index) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Session not found"
        );
    }
    
    // For upload sessions, handle chunk requests
    auto& session = it->second;
    session->mark_chunk_requested(chunk_index);
    
    return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
}

hypershare::crypto::CryptoResult TransferManager::handle_chunk_received(const std::string& session_id,
                                                                         uint32_t chunk_index,
                                                                         const std::vector<uint8_t>& chunk_data) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Session not found"
        );
    }
    
    auto& session = it->second;
    auto result = session->handle_chunk_received(chunk_index, chunk_data);
    
    if (result.success()) {
        total_bytes_transferred_ += chunk_data.size();
        
        // Check if transfer is complete
        if (session->is_complete()) {
            // Keep session for stats but mark as completed
            session->set_state(TransferState::COMPLETED);
        }
    }
    
    return result;
}

void TransferManager::set_max_concurrent_transfers(uint32_t max_transfers) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    max_concurrent_transfers_ = max_transfers;
}

void TransferManager::set_global_bandwidth_limit(uint64_t bytes_per_second) {
    global_bandwidth_limit_ = bytes_per_second;
}

uint32_t TransferManager::get_active_transfer_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    
    uint32_t active_count = 0;
    for (const auto& [session_id, session] : active_sessions_) {
        if (session->get_state() == TransferState::TRANSFERRING) {
            active_count++;
        }
    }
    
    return active_count;
}

uint64_t TransferManager::get_total_bytes_transferred() const {
    return total_bytes_transferred_;
}

double TransferManager::get_average_transfer_speed() const {
    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    
    if (elapsed_seconds == 0) return 0.0;
    
    return static_cast<double>(total_bytes_transferred_) / elapsed_seconds;
}

std::string TransferManager::generate_session_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, UINT32_MAX);
    
    std::ostringstream oss;
    oss << "session_" << std::hex << dis(gen);
    return oss.str();
}

void TransferManager::cleanup_completed_sessions() {
    // Remove completed sessions older than a certain age
    auto cutoff_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
    
    for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
        const auto& session = it->second;
        if (session->get_state() == TransferState::COMPLETED ||
            session->get_state() == TransferState::FAILED ||
            session->get_state() == TransferState::CANCELLED) {
            // In a real implementation, we'd check the completion time
            ++it; // Keep for now, implement proper cleanup later
        } else {
            ++it;
        }
    }
}

bool TransferManager::can_start_new_transfer() const {
    return active_sessions_.size() < max_concurrent_transfers_;
}

SessionStats TransferManager::create_session_stats(const TransferSession& session) {
    SessionStats stats;
    stats.session_id = session.get_session_id();
    stats.file_id = session.get_file_id();
    stats.peer_id = session.get_peer_id();
    stats.state = session.get_state();
    stats.progress_percentage = session.get_progress_percentage();
    stats.bytes_transferred = session.get_bytes_transferred();
    stats.total_bytes = 0; // Would need to get from metadata
    stats.start_time = std::chrono::steady_clock::now(); // Placeholder
    stats.estimated_time_remaining = std::chrono::milliseconds(0); // Calculate based on speed
    
    return stats;
}

} // namespace hypershare::transfer