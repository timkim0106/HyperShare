#include "hypershare/transfer/bandwidth_limiter.hpp"
#include <algorithm>

namespace hypershare::transfer {

BandwidthLimiter::BandwidthLimiter()
    : max_bandwidth_(1024 * 1024) // 1MB/s default
    , bucket_capacity_(64 * 1024)  // 64KB default bucket
    , available_tokens_(bucket_capacity_)
    , last_refill_(std::chrono::steady_clock::now())
{
}

void BandwidthLimiter::set_max_bandwidth(uint64_t bytes_per_second) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_bandwidth_ = bytes_per_second;
}

void BandwidthLimiter::set_bucket_capacity(uint64_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    bucket_capacity_ = capacity;
    available_tokens_ = std::min(available_tokens_, bucket_capacity_);
}

bool BandwidthLimiter::can_send(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    update_tokens();
    return available_tokens_ >= bytes;
}

void BandwidthLimiter::consume_tokens(uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (available_tokens_ >= bytes) {
        available_tokens_ -= bytes;
    }
}

void BandwidthLimiter::refill_bucket() {
    std::lock_guard<std::mutex> lock(mutex_);
    update_tokens();
}

void BandwidthLimiter::add_request(Priority priority, uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Request request;
    request.priority = priority;
    request.bytes = bytes;
    request.timestamp = std::chrono::steady_clock::now();
    
    pending_requests_.push(request);
}

std::vector<std::pair<BandwidthLimiter::Priority, uint64_t>> BandwidthLimiter::process_pending_requests() {
    std::lock_guard<std::mutex> lock(mutex_);
    update_tokens();
    
    std::vector<std::pair<Priority, uint64_t>> processed;
    
    while (!pending_requests_.empty() && available_tokens_ > 0) {
        auto request = pending_requests_.top();
        
        if (available_tokens_ >= request.bytes) {
            pending_requests_.pop();
            available_tokens_ -= request.bytes;
            processed.emplace_back(request.priority, request.bytes);
        } else {
            break; // Not enough tokens for next request
        }
    }
    
    return processed;
}

uint64_t BandwidthLimiter::get_pending_requests_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_requests_.size();
}

void BandwidthLimiter::update_tokens() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);
    
    if (elapsed.count() > 0) {
        // Calculate tokens to add based on elapsed time and bandwidth
        uint64_t tokens_to_add = (max_bandwidth_ * elapsed.count()) / 1000;
        
        available_tokens_ = std::min(available_tokens_ + tokens_to_add, bucket_capacity_);
        last_refill_ = now;
    }
}

} // namespace hypershare::transfer