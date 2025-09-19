#include "hypershare/transfer/performance_monitor.hpp"
#include <algorithm>

namespace hypershare::transfer {

PerformanceMonitor::PerformanceMonitor() {
}

void PerformanceMonitor::start_session(const std::string& session_id, uint64_t total_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session_id] = SessionData(total_bytes);
}

void PerformanceMonitor::end_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
}

void PerformanceMonitor::on_bytes_transferred(const std::string& session_id, uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        auto& session = it->second;
        session.bytes_transferred += bytes;
        session.last_update = std::chrono::steady_clock::now();
        
        // Record transfer event for speed calculation
        session.transfer_history.emplace_back(session.last_update, bytes);
        
        // Clean up old history to maintain window
        cleanup_old_history(session);
    }
}

void PerformanceMonitor::update_statistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [session_id, session] : sessions_) {
        calculate_speed(session);
        cleanup_old_history(session);
    }
}

SessionStats PerformanceMonitor::get_session_stats(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SessionStats stats;
    stats.session_id = session_id;
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        const auto& session = it->second;
        
        stats.total_bytes = session.total_bytes;
        stats.bytes_transferred = session.bytes_transferred;
        stats.percentage_complete = session.total_bytes > 0 ? 
            (static_cast<double>(session.bytes_transferred) / session.total_bytes) * 100.0 : 0.0;
        stats.current_speed_bps = session.current_speed_bps;
        stats.average_speed_bps = session.average_speed_bps;
        stats.start_time = session.start_time;
        stats.last_update = session.last_update;
        stats.estimated_time_remaining = calculate_eta(session);
    } else {
        // Default stats for non-existent session
        stats.total_bytes = 0;
        stats.bytes_transferred = 0;
        stats.percentage_complete = 0.0;
        stats.current_speed_bps = 0;
        stats.average_speed_bps = 0;
        stats.start_time = std::chrono::steady_clock::now();
        stats.last_update = stats.start_time;
        stats.estimated_time_remaining = std::chrono::milliseconds(0);
    }
    
    return stats;
}

std::vector<SessionStats> PerformanceMonitor::get_all_session_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<SessionStats> all_stats;
    for (const auto& [session_id, session] : sessions_) {
        SessionStats stats;
        stats.session_id = session_id;
        stats.total_bytes = session.total_bytes;
        stats.bytes_transferred = session.bytes_transferred;
        stats.percentage_complete = session.total_bytes > 0 ? 
            (static_cast<double>(session.bytes_transferred) / session.total_bytes) * 100.0 : 0.0;
        stats.current_speed_bps = session.current_speed_bps;
        stats.average_speed_bps = session.average_speed_bps;
        stats.start_time = session.start_time;
        stats.last_update = session.last_update;
        stats.estimated_time_remaining = calculate_eta(session);
        
        all_stats.push_back(stats);
    }
    
    return all_stats;
}

uint64_t PerformanceMonitor::get_total_bytes_transferred() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t total = 0;
    for (const auto& [session_id, session] : sessions_) {
        total += session.bytes_transferred;
    }
    
    return total;
}

uint64_t PerformanceMonitor::get_current_global_speed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t total_speed = 0;
    for (const auto& [session_id, session] : sessions_) {
        total_speed += session.current_speed_bps;
    }
    
    return total_speed;
}

void PerformanceMonitor::calculate_speed(SessionData& session) {
    auto now = std::chrono::steady_clock::now();
    
    // Calculate current speed (last second)
    auto one_second_ago = now - std::chrono::seconds(1);
    uint64_t recent_bytes = 0;
    
    for (const auto& [timestamp, bytes] : session.transfer_history) {
        if (timestamp >= one_second_ago) {
            recent_bytes += bytes;
        }
    }
    session.current_speed_bps = recent_bytes;
    
    // Calculate average speed (from start)
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - session.start_time);
    if (elapsed.count() > 0) {
        session.average_speed_bps = (session.bytes_transferred * 1000) / elapsed.count();
    }
}

std::chrono::milliseconds PerformanceMonitor::calculate_eta(const SessionData& session) {
    if (session.bytes_transferred >= session.total_bytes) {
        return std::chrono::milliseconds(0); // Complete
    }
    
    uint64_t remaining_bytes = session.total_bytes - session.bytes_transferred;
    
    // Use current speed if available, fall back to average speed
    uint64_t speed = session.current_speed_bps > 0 ? session.current_speed_bps : session.average_speed_bps;
    
    if (speed == 0) {
        return std::chrono::milliseconds(0); // Can't calculate ETA
    }
    
    // Calculate ETA in milliseconds
    uint64_t eta_ms = (remaining_bytes * 1000) / speed;
    return std::chrono::milliseconds(eta_ms);
}

void PerformanceMonitor::cleanup_old_history(SessionData& session) {
    auto cutoff = std::chrono::steady_clock::now() - HISTORY_WINDOW;
    
    while (!session.transfer_history.empty() && 
           session.transfer_history.front().first < cutoff) {
        session.transfer_history.pop_front();
    }
}

} // namespace hypershare::transfer