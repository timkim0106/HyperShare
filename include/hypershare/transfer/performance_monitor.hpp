#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <deque>
#include <cstdint>

namespace hypershare::transfer {

struct SessionStats {
    std::string session_id;
    uint64_t total_bytes;
    uint64_t bytes_transferred;
    double percentage_complete;
    uint64_t current_speed_bps;
    uint64_t average_speed_bps;
    std::chrono::milliseconds estimated_time_remaining;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
};

class PerformanceMonitor {
public:
    PerformanceMonitor();
    
    // Session management
    void start_session(const std::string& session_id, uint64_t total_bytes);
    void end_session(const std::string& session_id);
    
    // Data tracking
    void on_bytes_transferred(const std::string& session_id, uint64_t bytes);
    void update_statistics();
    
    // Statistics retrieval
    SessionStats get_session_stats(const std::string& session_id);
    std::vector<SessionStats> get_all_session_stats();
    
    // Global statistics
    uint64_t get_total_bytes_transferred() const;
    uint64_t get_current_global_speed() const;
    
private:
    struct SessionData {
        uint64_t total_bytes;
        uint64_t bytes_transferred;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_update;
        
        // Speed calculation data
        std::deque<std::pair<std::chrono::steady_clock::time_point, uint64_t>> transfer_history;
        uint64_t current_speed_bps;
        uint64_t average_speed_bps;
        
        SessionData() = default;
        
        SessionData(uint64_t total) 
            : total_bytes(total)
            , bytes_transferred(0)
            , start_time(std::chrono::steady_clock::now())
            , last_update(start_time)
            , current_speed_bps(0)
            , average_speed_bps(0) {}
    };
    
    std::unordered_map<std::string, SessionData> sessions_;
    mutable std::mutex mutex_;
    
    // Statistics calculation
    void calculate_speed(SessionData& session);
    std::chrono::milliseconds calculate_eta(const SessionData& session);
    void cleanup_old_history(SessionData& session);
    
    static constexpr std::chrono::seconds HISTORY_WINDOW{30}; // 30 second window
    static constexpr std::chrono::seconds SPEED_CALCULATION_INTERVAL{1}; // Update every second
};

} // namespace hypershare::transfer