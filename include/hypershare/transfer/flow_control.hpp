#pragma once

#include <chrono>
#include <cstdint>
#include <queue>

namespace hypershare::transfer {

class FlowController {
public:
    FlowController();
    
    // Congestion control
    uint32_t get_window_size() const { return window_size_; }
    void on_ack_received();
    void on_timeout();
    
    // RTT estimation
    void update_rtt(std::chrono::milliseconds rtt);
    std::chrono::milliseconds get_estimated_rtt() const { return estimated_rtt_; }
    std::chrono::milliseconds get_timeout() const;
    
    // Rate limiting
    void set_max_requests_per_second(uint32_t max_rate);
    bool can_send_request();
    void on_request_sent();
    void update_rate_limit();
    
private:
    // Congestion control state
    uint32_t window_size_;
    uint32_t slow_start_threshold_;
    uint32_t congestion_window_;
    bool in_slow_start_;
    
    // RTT tracking
    std::chrono::milliseconds estimated_rtt_;
    std::chrono::milliseconds rtt_variance_;
    std::chrono::milliseconds min_rtt_;
    
    // Rate limiting
    uint32_t max_requests_per_second_;
    std::queue<std::chrono::steady_clock::time_point> recent_requests_;
    std::chrono::steady_clock::time_point last_rate_update_;
    
    void update_congestion_window();
    void enter_congestion_avoidance();
    void handle_timeout_event();
};

} // namespace hypershare::transfer