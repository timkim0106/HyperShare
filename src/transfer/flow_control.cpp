#include "hypershare/transfer/flow_control.hpp"
#include <algorithm>

namespace hypershare::transfer {

FlowController::FlowController()
    : window_size_(1)
    , slow_start_threshold_(1024)
    , congestion_window_(1)
    , in_slow_start_(true)
    , estimated_rtt_(std::chrono::milliseconds(100))
    , rtt_variance_(std::chrono::milliseconds(50))
    , min_rtt_(std::chrono::milliseconds(10))
    , max_requests_per_second_(1000)
    , last_rate_update_(std::chrono::steady_clock::now())
{
}

void FlowController::on_ack_received() {
    if (in_slow_start_) {
        // Exponential growth in slow start
        congestion_window_++;
        if (congestion_window_ >= slow_start_threshold_) {
            enter_congestion_avoidance();
        }
    } else {
        // Linear growth in congestion avoidance
        // Increase by 1/cwnd for each ACK (approximate)
        static uint32_t ack_count = 0;
        ack_count++;
        if (ack_count >= congestion_window_) {
            congestion_window_++;
            ack_count = 0;
        }
    }
    
    update_congestion_window();
}

void FlowController::on_timeout() {
    handle_timeout_event();
}

void FlowController::update_rtt(std::chrono::milliseconds rtt) {
    // Update minimum RTT
    min_rtt_ = std::min(min_rtt_, rtt);
    
    // Exponential weighted moving average for RTT estimation
    const double alpha = 0.125; // Standard TCP value
    auto new_estimated_rtt = std::chrono::milliseconds(
        static_cast<long>((1.0 - alpha) * estimated_rtt_.count() + alpha * rtt.count())
    );
    
    // Update RTT variance
    auto rtt_diff = std::abs(rtt.count() - estimated_rtt_.count());
    rtt_variance_ = std::chrono::milliseconds(
        static_cast<long>((1.0 - alpha) * rtt_variance_.count() + alpha * rtt_diff)
    );
    
    estimated_rtt_ = new_estimated_rtt;
}

std::chrono::milliseconds FlowController::get_timeout() const {
    // Timeout = RTT + 4 * RTTVar (standard TCP formula)
    auto timeout = estimated_rtt_ + std::chrono::milliseconds(4 * rtt_variance_.count());
    
    // Ensure minimum timeout
    const auto min_timeout = std::chrono::milliseconds(100);
    return std::max(timeout, min_timeout);
}

void FlowController::set_max_requests_per_second(uint32_t max_rate) {
    max_requests_per_second_ = max_rate;
}

bool FlowController::can_send_request() {
    update_rate_limit();
    
    // Check if we're under the rate limit
    auto now = std::chrono::steady_clock::now();
    auto one_second_ago = now - std::chrono::seconds(1);
    
    // Count recent requests
    uint32_t recent_count = 0;
    auto temp_queue = recent_requests_;
    while (!temp_queue.empty()) {
        if (temp_queue.front() >= one_second_ago) {
            recent_count++;
        }
        temp_queue.pop();
    }
    
    return recent_count < max_requests_per_second_;
}

void FlowController::on_request_sent() {
    auto now = std::chrono::steady_clock::now();
    recent_requests_.push(now);
}

void FlowController::update_rate_limit() {
    auto now = std::chrono::steady_clock::now();
    auto one_second_ago = now - std::chrono::seconds(1);
    
    // Remove old requests from the queue
    while (!recent_requests_.empty() && recent_requests_.front() < one_second_ago) {
        recent_requests_.pop();
    }
    
    last_rate_update_ = now;
}

void FlowController::update_congestion_window() {
    window_size_ = std::min(congestion_window_, slow_start_threshold_);
    
    // Ensure minimum window size
    window_size_ = std::max(window_size_, 1u);
}

void FlowController::enter_congestion_avoidance() {
    in_slow_start_ = false;
}

void FlowController::handle_timeout_event() {
    // Multiplicative decrease: cut window in half
    slow_start_threshold_ = std::max(congestion_window_ / 2, 2u);
    congestion_window_ = 1;
    in_slow_start_ = true;
    
    update_congestion_window();
}

} // namespace hypershare::transfer