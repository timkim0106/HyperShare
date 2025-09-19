#include "hypershare/transfer/transfer_session.hpp"
#include "hypershare/crypto/hash.hpp"
#include <algorithm>
#include <random>

namespace hypershare::transfer {

TransferSession::TransferSession(const std::string& session_id, const std::string& file_id, uint32_t peer_id)
    : session_id_(session_id)
    , file_id_(file_id)
    , peer_id_(peer_id)
    , state_(TransferState::INACTIVE)
    , chunk_timeout_(std::chrono::seconds(30))
    , next_chunk_to_request_(0)
    , bytes_transferred_(0)
{
    chunk_request_times_.resize(1024);
}

void TransferSession::start_transfer(const hypershare::storage::FileMetadata& metadata) {
    metadata_ = metadata;
    state_ = TransferState::REQUESTING;
    start_time_ = std::chrono::steady_clock::now();
    
    // Reset tracking state
    requested_chunks_.reset();
    received_chunks_.reset();
    next_chunk_to_request_ = 0;
    bytes_transferred_ = 0;
    
    // Resize tracking arrays based on actual chunk count
    chunk_request_times_.resize(metadata_.chunk_count);
}

void TransferSession::set_state(TransferState new_state) {
    state_ = new_state;
}

bool TransferSession::is_complete() const {
    return state_ == TransferState::COMPLETED || 
           received_chunks_.count() == metadata_.chunk_count;
}

double TransferSession::get_progress_percentage() const {
    if (metadata_.chunk_count == 0) return 0.0;
    
    return (static_cast<double>(received_chunks_.count()) / metadata_.chunk_count) * 100.0;
}

uint64_t TransferSession::get_bytes_transferred() const {
    return bytes_transferred_;
}

hypershare::crypto::CryptoResult TransferSession::request_next_chunks(uint32_t window_size) {
    if (state_ != TransferState::REQUESTING && state_ != TransferState::TRANSFERRING) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Cannot request chunks in current state"
        );
    }
    
    uint32_t chunks_requested = 0;
    
    while (chunks_requested < window_size && next_chunk_to_request_ < metadata_.chunk_count) {
        // Skip chunks that are already requested or received
        if (!is_chunk_requested(next_chunk_to_request_) && 
            !is_chunk_received(next_chunk_to_request_)) {
            
            mark_chunk_requested(next_chunk_to_request_);
            chunks_requested++;
        }
        next_chunk_to_request_++;
    }
    
    if (state_ == TransferState::REQUESTING && chunks_requested > 0) {
        state_ = TransferState::TRANSFERRING;
    }
    
    return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
}

void TransferSession::mark_chunk_requested(uint32_t chunk_index) {
    if (chunk_index < metadata_.chunk_count) {
        requested_chunks_.set(chunk_index);
        chunk_request_times_[chunk_index] = std::chrono::steady_clock::now();
    }
}

hypershare::crypto::CryptoResult TransferSession::handle_chunk_received(uint32_t chunk_index,
                                                                         const std::vector<uint8_t>& chunk_data) {
    // Validate chunk index
    if (chunk_index >= metadata_.chunk_count) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Chunk index out of range"
        );
    }
    
    // Check if chunk was requested
    if (!is_chunk_requested(chunk_index)) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Chunk was not requested"
        );
    }
    
    // Validate chunk size (except for last chunk)
    uint32_t expected_size = metadata_.chunk_size;
    if (chunk_index == metadata_.chunk_count - 1) {
        // Last chunk might be smaller
        uint32_t remainder = metadata_.file_size % metadata_.chunk_size;
        if (remainder != 0) {
            expected_size = remainder;
        }
    }
    
    if (chunk_data.size() != expected_size) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Chunk size mismatch"
        );
    }
    
    // Validate chunk hash if available
    if (!validate_chunk(chunk_index, chunk_data)) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::VERIFICATION_FAILED,
            "Chunk hash verification failed"
        );
    }
    
    // Mark chunk as received
    received_chunks_.set(chunk_index);
    bytes_transferred_ += chunk_data.size();
    
    update_progress();
    
    // Check if transfer is complete
    if (received_chunks_.count() == metadata_.chunk_count) {
        state_ = TransferState::COMPLETED;
    }
    
    return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
}

bool TransferSession::is_chunk_requested(uint32_t chunk_index) const {
    return chunk_index < metadata_.chunk_count && requested_chunks_.test(chunk_index);
}

bool TransferSession::is_chunk_received(uint32_t chunk_index) const {
    return chunk_index < metadata_.chunk_count && received_chunks_.test(chunk_index);
}

void TransferSession::set_chunk_timeout(std::chrono::milliseconds timeout) {
    chunk_timeout_ = timeout;
}

std::vector<uint32_t> TransferSession::get_timed_out_chunks() {
    std::vector<uint32_t> timed_out;
    auto now = std::chrono::steady_clock::now();
    
    for (uint32_t i = 0; i < metadata_.chunk_count; ++i) {
        if (is_chunk_requested(i) && !is_chunk_received(i)) {
            auto elapsed = now - chunk_request_times_[i];
            if (elapsed > chunk_timeout_) {
                timed_out.push_back(i);
            }
        }
    }
    
    return timed_out;
}

hypershare::crypto::CryptoResult TransferSession::retry_chunk(uint32_t chunk_index) {
    if (chunk_index >= metadata_.chunk_count) {
        return hypershare::crypto::CryptoResult(
            hypershare::crypto::CryptoError::INVALID_STATE,
            "Chunk index out of range"
        );
    }
    
    // Reset the request time for retry
    chunk_request_times_[chunk_index] = std::chrono::steady_clock::now();
    
    return hypershare::crypto::CryptoResult(hypershare::crypto::CryptoError::SUCCESS);
}

void TransferSession::update_progress() {
    // Progress is automatically calculated based on received chunks
    // This method can be extended for additional progress tracking
}

bool TransferSession::validate_chunk(uint32_t chunk_index, const std::vector<uint8_t>& chunk_data) {
    // If we have hash information, validate it
    if (chunk_index < metadata_.chunk_hashes.size() && !metadata_.chunk_hashes[chunk_index].empty()) {
        std::span<const uint8_t> data_span(chunk_data.data(), chunk_data.size());
        auto computed_hash = hypershare::crypto::Blake3Hasher::hash(data_span);
        auto computed_hex = hypershare::crypto::hash_utils::hash_to_hex(computed_hash);
        
        return computed_hex == metadata_.chunk_hashes[chunk_index];
    }
    
    // If no hash available, just validate size
    return true;
}

} // namespace hypershare::transfer