#pragma once

#include <chrono>
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>

namespace hypershare::transfer {

class BandwidthLimiter {
public:
    enum class Priority {
        LOW = 0,
        NORMAL = 1,
        HIGH = 2
    };
    
    BandwidthLimiter();
    
    // Configuration
    void set_max_bandwidth(uint64_t bytes_per_second);
    void set_bucket_capacity(uint64_t capacity);
    
    // Token bucket operations
    bool can_send(uint64_t bytes);
    void consume_tokens(uint64_t bytes);
    void refill_bucket();
    
    // Priority queue operations
    void add_request(Priority priority, uint64_t bytes);
    std::vector<std::pair<Priority, uint64_t>> process_pending_requests();
    
    // Statistics
    uint64_t get_max_bandwidth() const { return max_bandwidth_; }
    uint64_t get_bucket_capacity() const { return bucket_capacity_; }
    uint64_t get_available_tokens() const { return available_tokens_; }
    uint64_t get_pending_requests_count() const;
    
private:
    struct Request {
        Priority priority;
        uint64_t bytes;
        std::chrono::steady_clock::time_point timestamp;
        
        bool operator<(const Request& other) const {
            // Higher priority first, then earlier timestamp
            if (priority != other.priority) {
                return priority < other.priority; // Reverse for max heap
            }
            return timestamp > other.timestamp; // Earlier timestamp first
        }
    };
    
    uint64_t max_bandwidth_;
    uint64_t bucket_capacity_;
    uint64_t available_tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    
    std::priority_queue<Request> pending_requests_;
    mutable std::mutex mutex_;
    
    void update_tokens();
};

} // namespace hypershare::transfer