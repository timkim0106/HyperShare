#pragma once

#include "transfer_session.hpp"
#include "../storage/storage_config.hpp"
#include "../crypto/crypto_types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace hypershare::transfer {

struct TransferSessionStats {
    std::string session_id;
    std::string file_id;
    uint32_t peer_id;
    TransferState state;
    double progress_percentage;
    uint64_t bytes_transferred;
    uint64_t total_bytes;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::milliseconds estimated_time_remaining;
};

class TransferManager {
public:
    explicit TransferManager(const hypershare::storage::StorageConfig& config);
    ~TransferManager();
    
    // Session management
    std::string start_download(const std::string& file_id, uint32_t peer_id);
    std::string start_upload(const std::string& file_id, uint32_t peer_id);
    
    bool has_session(const std::string& session_id);
    TransferSessionStats get_session_stats(const std::string& session_id);
    std::vector<TransferSessionStats> get_all_sessions();
    
    // Transfer control
    hypershare::crypto::CryptoResult pause_transfer(const std::string& session_id);
    hypershare::crypto::CryptoResult resume_transfer(const std::string& session_id);
    hypershare::crypto::CryptoResult cancel_transfer(const std::string& session_id);
    
    // Chunk handling
    hypershare::crypto::CryptoResult handle_chunk_request(const std::string& session_id,
                                                           uint32_t chunk_index);
    hypershare::crypto::CryptoResult handle_chunk_received(const std::string& session_id,
                                                            uint32_t chunk_index,
                                                            const std::vector<uint8_t>& chunk_data);
    
    // Configuration
    void set_max_concurrent_transfers(uint32_t max_transfers);
    void set_global_bandwidth_limit(uint64_t bytes_per_second);
    
    // Statistics
    uint32_t get_active_transfer_count() const;
    uint64_t get_total_bytes_transferred() const;
    double get_average_transfer_speed() const; // bytes per second
    
private:
    hypershare::storage::StorageConfig config_;
    std::unordered_map<std::string, std::unique_ptr<TransferSession>> active_sessions_;
    mutable std::mutex sessions_mutex_;
    
    uint32_t max_concurrent_transfers_;
    uint64_t global_bandwidth_limit_;
    uint64_t total_bytes_transferred_;
    std::chrono::steady_clock::time_point start_time_;
    
    std::string generate_session_id();
    void cleanup_completed_sessions();
    bool can_start_new_transfer() const;
    TransferSessionStats create_session_stats(const TransferSession& session);
};

} // namespace hypershare::transfer