#include <gtest/gtest.h>
#include "hypershare/transfer/transfer_session.hpp"
#include "hypershare/transfer/transfer_manager.hpp"
#include "hypershare/transfer/flow_control.hpp"
#include "hypershare/storage/file_metadata.hpp"
#include <chrono>
#include <thread>

using namespace hypershare::transfer;
using namespace hypershare::storage;

class TransferSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test file metadata
        test_metadata_.file_id = "test_file_123";
        test_metadata_.filename = "test.txt";
        test_metadata_.file_size = 1024 * 1024; // 1MB
        test_metadata_.chunk_size = 64 * 1024;  // 64KB chunks
        test_metadata_.chunk_count = 16;        // 16 chunks
        test_metadata_.chunk_hashes.resize(16);
        
        // Fill with dummy hashes
        for (auto& hash : test_metadata_.chunk_hashes) {
            std::fill(hash.begin(), hash.end(), 0x42);
        }
    }
    
    FileMetadata test_metadata_;
};

// Test TransferSession state management
TEST_F(TransferSessionTest, TransferSession_StateTransitions) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    
    // Initial state
    EXPECT_EQ(session.get_state(), TransferState::INACTIVE);
    EXPECT_EQ(session.get_progress_percentage(), 0.0);
    EXPECT_EQ(session.get_bytes_transferred(), 0);
    
    // Start transfer
    session.start_transfer(test_metadata_);
    EXPECT_EQ(session.get_state(), TransferState::REQUESTING);
    
    // Begin downloading
    session.set_state(TransferState::TRANSFERRING);
    EXPECT_EQ(session.get_state(), TransferState::TRANSFERRING);
    
    // Complete transfer
    session.set_state(TransferState::COMPLETED);
    EXPECT_EQ(session.get_state(), TransferState::COMPLETED);
    EXPECT_TRUE(session.is_complete());
}

TEST_F(TransferSessionTest, TransferSession_ChunkTracking) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    // Initially no chunks requested or received
    EXPECT_EQ(session.get_requested_chunks().count(), 0);
    EXPECT_EQ(session.get_received_chunks().count(), 0);
    
    // Request some chunks
    session.mark_chunk_requested(0);
    session.mark_chunk_requested(1);
    session.mark_chunk_requested(5);
    
    EXPECT_EQ(session.get_requested_chunks().count(), 3);
    EXPECT_TRUE(session.is_chunk_requested(0));
    EXPECT_TRUE(session.is_chunk_requested(1));
    EXPECT_FALSE(session.is_chunk_requested(2));
    
    // Receive some chunks
    std::vector<uint8_t> chunk_data(test_metadata_.chunk_size, 0x42);
    
    auto result = session.handle_chunk_received(0, chunk_data);
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(session.is_chunk_received(0));
    EXPECT_EQ(session.get_received_chunks().count(), 1);
    
    // Progress should update
    double expected_progress = (1.0 / test_metadata_.chunk_count) * 100.0;
    EXPECT_NEAR(session.get_progress_percentage(), expected_progress, 0.1);
}

TEST_F(TransferSessionTest, TransferSession_WindowBasedRequests) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    // Request chunks with window size
    uint32_t window_size = 4;
    auto result = session.request_next_chunks(window_size);
    EXPECT_TRUE(result.success());
    
    // Should have requested first 4 chunks
    EXPECT_EQ(session.get_requested_chunks().count(), window_size);
    for (uint32_t i = 0; i < window_size; ++i) {
        EXPECT_TRUE(session.is_chunk_requested(i));
    }
    
    // Request more chunks (should get next batch)
    result = session.request_next_chunks(window_size);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(session.get_requested_chunks().count(), window_size * 2);
}

TEST_F(TransferSessionTest, TransferSession_ProgressCalculations) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    // Simulate receiving chunks
    std::vector<uint8_t> chunk_data(test_metadata_.chunk_size, 0x42);
    
    // Receive 25% of chunks
    for (uint64_t i = 0; i < test_metadata_.chunk_count / 4; ++i) {
        session.handle_chunk_received(i, chunk_data);
    }
    
    EXPECT_NEAR(session.get_progress_percentage(), 25.0, 1.0);
    EXPECT_EQ(session.get_bytes_transferred(), (test_metadata_.chunk_count / 4) * test_metadata_.chunk_size);
    
    // Receive all remaining chunks
    for (uint64_t i = test_metadata_.chunk_count / 4; i < test_metadata_.chunk_count; ++i) {
        session.handle_chunk_received(i, chunk_data);
    }
    
    EXPECT_NEAR(session.get_progress_percentage(), 100.0, 0.1);
    EXPECT_EQ(session.get_bytes_transferred(), test_metadata_.file_size);
    EXPECT_TRUE(session.is_complete());
}

TEST_F(TransferSessionTest, TransferSession_Timeouts) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    // Set short timeout for testing
    session.set_chunk_timeout(std::chrono::milliseconds(100));
    
    // Request a chunk
    session.mark_chunk_requested(0);
    
    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Check for timeouts
    auto timed_out_chunks = session.get_timed_out_chunks();
    EXPECT_EQ(timed_out_chunks.size(), 1);
    EXPECT_EQ(timed_out_chunks[0], 0);
    
    // Retry should work
    auto result = session.retry_chunk(0);
    EXPECT_TRUE(result.success());
}

// Test FlowController functionality
TEST_F(TransferSessionTest, FlowController_CongestionControl) {
    FlowController controller;
    
    // Initial window size should be small
    EXPECT_EQ(controller.get_window_size(), 1);
    
    // Simulate successful transmissions (slow start)
    for (int i = 0; i < 10; ++i) {
        controller.on_ack_received();
    }
    
    // Window should have grown exponentially
    EXPECT_GT(controller.get_window_size(), 1);
    
    uint32_t window_before_loss = controller.get_window_size();
    
    // Simulate packet loss
    controller.on_timeout();
    
    // Window should have reduced
    EXPECT_LT(controller.get_window_size(), window_before_loss);
}

TEST_F(TransferSessionTest, FlowController_RTTEstimation) {
    FlowController controller;
    
    // Simulate RTT measurements
    controller.update_rtt(std::chrono::milliseconds(50));
    controller.update_rtt(std::chrono::milliseconds(60));
    controller.update_rtt(std::chrono::milliseconds(40));
    
    auto estimated_rtt = controller.get_estimated_rtt();
    EXPECT_GT(estimated_rtt, std::chrono::milliseconds(40));
    EXPECT_LT(estimated_rtt, std::chrono::milliseconds(60));
    
    // Timeout should be based on RTT
    auto timeout = controller.get_timeout();
    EXPECT_GT(timeout, estimated_rtt);
}

TEST_F(TransferSessionTest, FlowController_RateLimiting) {
    FlowController controller;
    controller.set_max_requests_per_second(10); // 10 requests per second
    
    // Should allow initial requests
    EXPECT_TRUE(controller.can_send_request());
    
    // Simulate rapid requests
    for (int i = 0; i < 15; ++i) {
        if (controller.can_send_request()) {
            controller.on_request_sent();
        }
    }
    
    // Should eventually be rate limited
    EXPECT_FALSE(controller.can_send_request());
    
    // Wait and should be able to send again
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    controller.update_rate_limit();
    EXPECT_TRUE(controller.can_send_request());
}

// Test TransferManager functionality
class TransferManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.download_directory = std::filesystem::temp_directory_path() / "transfer_test";
        std::filesystem::create_directories(config_.download_directory);
        
        transfer_manager_ = std::make_unique<TransferManager>(config_);
    }
    
    void TearDown() override {
        std::filesystem::remove_all(config_.download_directory);
    }
    
    StorageConfig config_;
    std::unique_ptr<TransferManager> transfer_manager_;
};

TEST_F(TransferManagerTest, TransferManager_SessionCreation) {
    FileMetadata metadata;
    metadata.file_id = "test_file";
    metadata.filename = "test.txt";
    metadata.file_size = 1024;
    
    // Start download
    auto session_id = transfer_manager_->start_download(metadata.file_id, 1001);
    EXPECT_FALSE(session_id.empty());
    
    // Session should exist
    EXPECT_TRUE(transfer_manager_->has_session(session_id));
    
    // Get session statistics
    auto stats = transfer_manager_->get_session_stats(session_id);
    EXPECT_EQ(stats.session_id, session_id);
    EXPECT_EQ(stats.file_id, metadata.file_id);
    EXPECT_EQ(stats.peer_id, 1001);
}

TEST_F(TransferManagerTest, TransferManager_MultipleTransfers) {
    // Start multiple downloads
    std::vector<std::string> session_ids;
    
    for (int i = 0; i < 5; ++i) {
        std::string file_id = "file_" + std::to_string(i);
        auto session_id = transfer_manager_->start_download(file_id, 1000 + i);
        session_ids.push_back(session_id);
    }
    
    // All sessions should be active
    auto all_sessions = transfer_manager_->get_all_sessions();
    EXPECT_EQ(all_sessions.size(), 5);
    
    // Cancel some transfers
    transfer_manager_->cancel_transfer(session_ids[0]);
    transfer_manager_->cancel_transfer(session_ids[2]);
    
    all_sessions = transfer_manager_->get_all_sessions();
    EXPECT_EQ(all_sessions.size(), 3);
}

TEST_F(TransferManagerTest, TransferManager_PauseResume) {
    std::string file_id = "pausable_file";
    auto session_id = transfer_manager_->start_download(file_id, 1001);
    
    // Pause transfer
    auto result = transfer_manager_->pause_transfer(session_id);
    EXPECT_TRUE(result.success());
    
    auto stats = transfer_manager_->get_session_stats(session_id);
    EXPECT_EQ(stats.state, TransferState::PAUSED);
    
    // Resume transfer
    result = transfer_manager_->resume_transfer(session_id);
    EXPECT_TRUE(result.success());
    
    stats = transfer_manager_->get_session_stats(session_id);
    EXPECT_EQ(stats.state, TransferState::TRANSFERRING);
}

// Test error conditions
TEST_F(TransferSessionTest, ErrorHandling_InvalidChunks) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    // Try to receive chunk that wasn't requested
    std::vector<uint8_t> chunk_data(test_metadata_.chunk_size, 0x42);
    auto result = session.handle_chunk_received(10, chunk_data);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, hypershare::crypto::CryptoError::INVALID_STATE);
    
    // Try to receive chunk with wrong size
    std::vector<uint8_t> wrong_size_data(100, 0x42);
    session.mark_chunk_requested(0);
    result = session.handle_chunk_received(0, wrong_size_data);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, hypershare::crypto::CryptoError::INVALID_STATE);
}

TEST_F(TransferSessionTest, ErrorHandling_DuplicateChunks) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    std::vector<uint8_t> chunk_data(test_metadata_.chunk_size, 0x42);
    session.mark_chunk_requested(0);
    
    // First reception should succeed
    auto result = session.handle_chunk_received(0, chunk_data);
    EXPECT_TRUE(result.success());
    
    // Duplicate reception should be handled gracefully
    result = session.handle_chunk_received(0, chunk_data);
    EXPECT_TRUE(result.success()); // Should not error, but should ignore
    
    // Should still only count as one chunk
    EXPECT_EQ(session.get_received_chunks().count(), 1);
}

// Performance tests
TEST_F(TransferSessionTest, Performance_ChunkHandling) {
    TransferSession session("session_123", test_metadata_.file_id, 1001);
    session.start_transfer(test_metadata_);
    
    std::vector<uint8_t> chunk_data(test_metadata_.chunk_size, 0x42);
    
    // Request all chunks
    for (uint64_t i = 0; i < test_metadata_.chunk_count; ++i) {
        session.mark_chunk_requested(i);
    }
    
    // Time chunk reception
    auto start = std::chrono::high_resolution_clock::now();
    
    for (uint64_t i = 0; i < test_metadata_.chunk_count; ++i) {
        session.handle_chunk_received(i, chunk_data);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should handle chunks quickly (less than 1ms per chunk)
    EXPECT_LT(duration.count(), test_metadata_.chunk_count * 1000);
    
    EXPECT_TRUE(session.is_complete());
}

// Stress tests
TEST_F(TransferSessionTest, Stress_LargeFileTransfer) {
    // Create metadata for large file (1GB with 1MB chunks)
    FileMetadata large_metadata;
    large_metadata.file_id = "large_file";
    large_metadata.filename = "large.bin";
    large_metadata.file_size = 1ULL << 30; // 1GB
    large_metadata.chunk_size = 1ULL << 20; // 1MB chunks
    large_metadata.chunk_count = 1024;      // 1024 chunks
    
    TransferSession session("large_session", large_metadata.file_id, 1001);
    session.start_transfer(large_metadata);
    
    // Should handle large number of chunks
    auto result = session.request_next_chunks(100);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(session.get_requested_chunks().count(), 100);
    
    // Simulate receiving 10% of chunks
    std::vector<uint8_t> chunk_data(large_metadata.chunk_size, 0x42);
    for (uint64_t i = 0; i < 100; ++i) {
        session.handle_chunk_received(i, chunk_data);
    }
    
    EXPECT_NEAR(session.get_progress_percentage(), 9.77, 0.1); // ~10%
}

TEST_F(TransferManagerTest, Stress_ConcurrentTransfers) {
    const int num_transfers = 50;
    std::vector<std::string> session_ids;
    
    // Start many concurrent transfers
    for (int i = 0; i < num_transfers; ++i) {
        std::string file_id = "concurrent_file_" + std::to_string(i);
        auto session_id = transfer_manager_->start_download(file_id, 2000 + i);
        session_ids.push_back(session_id);
    }
    
    EXPECT_EQ(transfer_manager_->get_all_sessions().size(), num_transfers);
    
    // Cancel half of them
    for (int i = 0; i < num_transfers / 2; ++i) {
        transfer_manager_->cancel_transfer(session_ids[i]);
    }
    
    EXPECT_EQ(transfer_manager_->get_all_sessions().size(), num_transfers / 2);
}