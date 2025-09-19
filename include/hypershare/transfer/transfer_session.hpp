#pragma once

#include "../storage/file_metadata.hpp"
#include "../crypto/crypto_types.hpp"
#include <string>
#include <vector>
#include <bitset>
#include <chrono>
#include <cstdint>

namespace hypershare::transfer {

enum class TransferState {
    INACTIVE,
    REQUESTING,
    TRANSFERRING,
    PAUSED,
    COMPLETED,
    FAILED,
    CANCELLED
};

class TransferSession {
public:
    TransferSession(const std::string& session_id, const std::string& file_id, uint32_t peer_id);
    
    // Session control
    void start_transfer(const hypershare::storage::FileMetadata& metadata);
    void set_state(TransferState new_state);
    TransferState get_state() const { return state_; }
    bool is_complete() const;
    
    // Progress tracking
    double get_progress_percentage() const;
    uint64_t get_bytes_transferred() const;
    
    // Chunk management
    hypershare::crypto::CryptoResult request_next_chunks(uint32_t window_size);
    void mark_chunk_requested(uint32_t chunk_index);
    hypershare::crypto::CryptoResult handle_chunk_received(uint32_t chunk_index, 
                                                           const std::vector<uint8_t>& chunk_data);
    
    // Chunk status queries
    std::bitset<1024> get_requested_chunks() const { return requested_chunks_; }
    std::bitset<1024> get_received_chunks() const { return received_chunks_; }
    bool is_chunk_requested(uint32_t chunk_index) const;
    bool is_chunk_received(uint32_t chunk_index) const;
    
    // Timeout handling
    void set_chunk_timeout(std::chrono::milliseconds timeout);
    std::vector<uint32_t> get_timed_out_chunks();
    hypershare::crypto::CryptoResult retry_chunk(uint32_t chunk_index);
    
    // Getters
    const std::string& get_session_id() const { return session_id_; }
    const std::string& get_file_id() const { return file_id_; }
    uint32_t get_peer_id() const { return peer_id_; }
    
private:
    std::string session_id_;
    std::string file_id_;
    uint32_t peer_id_;
    TransferState state_;
    
    hypershare::storage::FileMetadata metadata_;
    std::bitset<1024> requested_chunks_;
    std::bitset<1024> received_chunks_;
    
    std::chrono::milliseconds chunk_timeout_;
    std::vector<std::chrono::steady_clock::time_point> chunk_request_times_;
    uint32_t next_chunk_to_request_;
    
    uint64_t bytes_transferred_;
    std::chrono::steady_clock::time_point start_time_;
    
    void update_progress();
    bool validate_chunk(uint32_t chunk_index, const std::vector<uint8_t>& chunk_data);
};

} // namespace hypershare::transfer