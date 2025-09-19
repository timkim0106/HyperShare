#include <gtest/gtest.h>
#include "hypershare/transfer/bandwidth_limiter.hpp"
#include "hypershare/transfer/performance_monitor.hpp"
#include "hypershare/storage/resume_manager.hpp"
#include "hypershare/crypto/file_verification.hpp"
#include "hypershare/storage/file_metadata.hpp"
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>

using namespace hypershare::transfer;
using namespace hypershare::storage;
using namespace hypershare::crypto;

class PerformanceReliabilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "perf_reliability_test";
        std::filesystem::create_directories(test_dir_);
        
        // Create test file
        test_file_path_ = test_dir_ / "test_file.bin";
        create_test_file(test_file_path_, 1024 * 1024); // 1MB file
        
        // Setup test metadata
        test_metadata_.file_id = "perf_test_file";
        test_metadata_.filename = "test_file.bin";
        test_metadata_.file_path = test_file_path_;
        test_metadata_.file_size = 1024 * 1024;
        test_metadata_.chunk_size = 64 * 1024; // 64KB chunks
        test_metadata_.chunk_count = 16;
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    
    void create_test_file(const std::filesystem::path& path, size_t size) {
        std::ofstream file(path, std::ios::binary);
        for (size_t i = 0; i < size; ++i) {
            file.put(static_cast<char>(i % 256));
        }
    }
    
    std::filesystem::path test_dir_;
    std::filesystem::path test_file_path_;
    FileMetadata test_metadata_;
};

// Test BandwidthLimiter functionality
TEST_F(PerformanceReliabilityTest, BandwidthLimiter_TokenBucket) {
    BandwidthLimiter limiter;
    limiter.set_max_bandwidth(1024 * 1024); // 1MB/s
    limiter.set_bucket_capacity(64 * 1024);  // 64KB bucket
    
    // Should allow initial requests up to bucket capacity
    EXPECT_TRUE(limiter.can_send(32 * 1024)); // 32KB
    limiter.consume_tokens(32 * 1024);
    
    EXPECT_TRUE(limiter.can_send(32 * 1024)); // Another 32KB
    limiter.consume_tokens(32 * 1024);
    
    // Should not allow more than bucket capacity
    EXPECT_FALSE(limiter.can_send(1024)); // Even 1KB should be rejected
    
    // Wait for token refill
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    limiter.refill_bucket();
    
    // Should allow requests again
    EXPECT_TRUE(limiter.can_send(16 * 1024));
}

TEST_F(PerformanceReliabilityTest, BandwidthLimiter_RateCalculation) {
    BandwidthLimiter limiter;
    limiter.set_max_bandwidth(100 * 1024); // 100KB/s
    
    auto start = std::chrono::steady_clock::now();
    
    // Consume tokens at maximum rate
    while (limiter.can_send(1024)) {
        limiter.consume_tokens(1024);
        limiter.refill_bucket();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    // Should take approximately the right amount of time for rate limiting
    EXPECT_GT(duration.count(), 0);
}

TEST_F(PerformanceReliabilityTest, BandwidthLimiter_QoSPriorities) {
    BandwidthLimiter limiter;
    limiter.set_max_bandwidth(64 * 1024); // 64KB/s
    
    // Add requests with different priorities
    limiter.add_request(BandwidthLimiter::Priority::HIGH, 16 * 1024);
    limiter.add_request(BandwidthLimiter::Priority::NORMAL, 16 * 1024);
    limiter.add_request(BandwidthLimiter::Priority::LOW, 16 * 1024);
    limiter.add_request(BandwidthLimiter::Priority::HIGH, 16 * 1024);
    
    // Process requests - high priority should go first
    auto processed = limiter.process_pending_requests();
    
    // Should process high priority requests first
    EXPECT_EQ(processed.size(), 4);
    EXPECT_EQ(processed[0].first, BandwidthLimiter::Priority::HIGH);
    EXPECT_EQ(processed[1].first, BandwidthLimiter::Priority::HIGH);
    // Order of NORMAL and LOW may vary depending on implementation
}

// Test PerformanceMonitor functionality
TEST_F(PerformanceReliabilityTest, PerformanceMonitor_StatisticsTracking) {
    PerformanceMonitor monitor;
    
    std::string session_id = "test_session_123";
    uint64_t total_size = 1024 * 1024; // 1MB
    
    // Initialize session
    monitor.start_session(session_id, total_size);
    
    // Simulate data transfer
    auto start_time = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 10; ++i) {
        monitor.on_bytes_transferred(session_id, 100 * 1024); // 100KB chunks
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        monitor.update_statistics();
    }
    
    auto stats = monitor.get_session_stats(session_id);
    
    EXPECT_EQ(stats.session_id, session_id);
    EXPECT_EQ(stats.total_bytes, total_size);
    EXPECT_EQ(stats.bytes_transferred, 1024 * 1024); // All transferred
    EXPECT_NEAR(stats.percentage_complete, 100.0, 0.1);
    EXPECT_GT(stats.current_speed_bps, 0);
    EXPECT_GT(stats.average_speed_bps, 0);
    EXPECT_EQ(stats.estimated_time_remaining.count(), 0); // Should be 0 when complete
}

TEST_F(PerformanceReliabilityTest, PerformanceMonitor_SpeedCalculation) {
    PerformanceMonitor monitor;
    
    std::string session_id = "speed_test";
    uint64_t total_size = 2 * 1024 * 1024; // 2MB
    
    monitor.start_session(session_id, total_size);
    
    // Transfer data at known rate
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 20; ++i) {
        monitor.on_bytes_transferred(session_id, 100 * 1024); // 100KB
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 100ms intervals
        monitor.update_statistics();
    }
    
    auto stats = monitor.get_session_stats(session_id);
    
    // Should calculate approximately 1MB/s (100KB every 100ms)
    EXPECT_GT(stats.current_speed_bps, 800 * 1024); // At least 800KB/s
    EXPECT_LT(stats.current_speed_bps, 1200 * 1024); // At most 1.2MB/s
}

TEST_F(PerformanceReliabilityTest, PerformanceMonitor_ETACalculation) {
    PerformanceMonitor monitor;
    
    std::string session_id = "eta_test";
    uint64_t total_size = 1024 * 1024; // 1MB
    
    monitor.start_session(session_id, total_size);
    
    // Transfer 50% of data
    monitor.on_bytes_transferred(session_id, 512 * 1024);
    monitor.update_statistics();
    
    auto stats = monitor.get_session_stats(session_id);
    
    EXPECT_NEAR(stats.percentage_complete, 50.0, 1.0);
    EXPECT_GT(stats.estimated_time_remaining.count(), 0);
    
    // Transfer remaining data
    monitor.on_bytes_transferred(session_id, 512 * 1024);
    monitor.update_statistics();
    
    stats = monitor.get_session_stats(session_id);
    EXPECT_NEAR(stats.percentage_complete, 100.0, 0.1);
    EXPECT_EQ(stats.estimated_time_remaining.count(), 0);
}

// Test ResumeManager functionality
TEST_F(PerformanceReliabilityTest, ResumeManager_SaveLoadState) {
    std::string resume_db = test_dir_ / "resume.db";
    ResumeManager resume_manager(resume_db);
    
    // Create resume info
    ResumeInfo info;
    info.file_id = test_metadata_.file_id;
    info.session_id = "resume_test_session";
    info.completed_chunks = {0, 1, 3, 5, 7}; // Some chunks completed
    info.last_activity = std::chrono::system_clock::now();
    info.stats.bytes_transferred = 5 * test_metadata_.chunk_size;
    info.stats.total_bytes = test_metadata_.file_size;
    
    // Save resume state
    auto result = resume_manager.save_resume_state(info);
    EXPECT_TRUE(result.success());
    
    // Load resume state
    ResumeInfo loaded_info;
    result = resume_manager.load_resume_state(test_metadata_.file_id, loaded_info);
    EXPECT_TRUE(result.success());
    
    EXPECT_EQ(info.file_id, loaded_info.file_id);
    EXPECT_EQ(info.session_id, loaded_info.session_id);
    EXPECT_EQ(info.completed_chunks, loaded_info.completed_chunks);
    EXPECT_EQ(info.stats.bytes_transferred, loaded_info.stats.bytes_transferred);
}

TEST_F(PerformanceReliabilityTest, ResumeManager_ResumableTransfers) {
    std::string resume_db = test_dir_ / "resume.db";
    ResumeManager resume_manager(resume_db);
    
    // Create multiple resume infos
    std::vector<ResumeInfo> resume_infos;
    
    for (int i = 0; i < 5; ++i) {
        ResumeInfo info;
        info.file_id = "file_" + std::to_string(i);
        info.session_id = "session_" + std::to_string(i);
        info.last_activity = std::chrono::system_clock::now() - std::chrono::hours(i);
        
        resume_manager.save_resume_state(info);
        resume_infos.push_back(info);
    }
    
    // Get resumable transfers
    auto resumable = resume_manager.get_resumable_transfers();
    EXPECT_EQ(resumable.size(), 5);
    
    // Cleanup old resume data (older than 2 hours)
    resume_manager.cleanup_old_resume_data(std::chrono::hours(2));
    
    resumable = resume_manager.get_resumable_transfers();
    EXPECT_LT(resumable.size(), 5); // Some should be cleaned up
}

TEST_F(PerformanceReliabilityTest, ResumeManager_ChunkProgressTracking) {
    std::string resume_db = test_dir_ / "resume.db";
    ResumeManager resume_manager(resume_db);
    
    ResumeInfo info;
    info.file_id = test_metadata_.file_id;
    info.session_id = "chunk_progress_test";
    
    // Simulate progressive chunk completion
    for (uint64_t i = 0; i < test_metadata_.chunk_count / 2; ++i) {
        info.completed_chunks.push_back(i);
        info.stats.bytes_transferred += test_metadata_.chunk_size;
        
        resume_manager.save_resume_state(info);
    }
    
    // Load and verify
    ResumeInfo loaded;
    resume_manager.load_resume_state(test_metadata_.file_id, loaded);
    
    EXPECT_EQ(loaded.completed_chunks.size(), test_metadata_.chunk_count / 2);
    EXPECT_EQ(loaded.stats.bytes_transferred, 
              (test_metadata_.chunk_count / 2) * test_metadata_.chunk_size);
}

// Test FileVerifier functionality
TEST_F(PerformanceReliabilityTest, FileVerifier_ChunkVerification) {
    FileVerifier verifier;
    
    // Create test chunk data
    std::vector<uint8_t> chunk_data(64 * 1024, 0x42);
    auto chunk_hash = Blake3Hasher::hash(chunk_data);
    
    // Correct chunk should verify
    auto result = verifier.verify_chunk(chunk_data, chunk_hash);
    EXPECT_TRUE(result.success());
    
    // Modified chunk should not verify
    chunk_data[100] = 0x43; // Modify one byte
    result = verifier.verify_chunk(chunk_data, chunk_hash);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::VERIFICATION_FAILED);
}

TEST_F(PerformanceReliabilityTest, FileVerifier_FileVerification) {
    FileVerifier verifier;
    
    // Create proper metadata for test file
    test_metadata_.file_hash = Blake3Hasher::hash_file(test_file_path_);
    test_metadata_.chunk_hashes.clear();
    
    // Calculate chunk hashes
    std::ifstream file(test_file_path_, std::ios::binary);
    for (uint64_t i = 0; i < test_metadata_.chunk_count; ++i) {
        std::vector<uint8_t> chunk_data(test_metadata_.chunk_size);
        file.read(reinterpret_cast<char*>(chunk_data.data()), test_metadata_.chunk_size);
        
        auto actual_size = file.gcount();
        chunk_data.resize(actual_size);
        
        test_metadata_.chunk_hashes.push_back(Blake3Hasher::hash(chunk_data));
    }
    
    // Verify complete file
    auto result = verifier.verify_file(test_file_path_, test_metadata_);
    EXPECT_TRUE(result.success());
}

TEST_F(PerformanceReliabilityTest, FileVerifier_CorruptionDetection) {
    FileVerifier verifier;
    
    // Create metadata with correct hashes
    test_metadata_.chunk_hashes.resize(test_metadata_.chunk_count);
    std::ifstream file(test_file_path_, std::ios::binary);
    
    for (uint64_t i = 0; i < test_metadata_.chunk_count; ++i) {
        std::vector<uint8_t> chunk_data(test_metadata_.chunk_size);
        file.read(reinterpret_cast<char*>(chunk_data.data()), test_metadata_.chunk_size);
        chunk_data.resize(file.gcount());
        test_metadata_.chunk_hashes[i] = Blake3Hasher::hash(chunk_data);
    }
    file.close();
    
    // Corrupt the file
    std::ofstream corrupt_file(test_file_path_, std::ios::binary | std::ios::in);
    corrupt_file.seekp(100 * 1024); // Go to middle of second chunk
    corrupt_file.put(0xFF); // Corrupt one byte
    corrupt_file.close();
    
    // Detect corruption
    std::vector<uint64_t> corrupted_chunks;
    auto result = verifier.detect_corruption(test_metadata_, corrupted_chunks);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(corrupted_chunks.size(), 1);
    EXPECT_EQ(corrupted_chunks[0], 1); // Second chunk should be corrupted
}

TEST_F(PerformanceReliabilityTest, FileVerifier_PerformanceWithLargeFile) {
    // Create larger test file for performance testing
    std::filesystem::path large_file = test_dir_ / "large_test.bin";
    create_test_file(large_file, 10 * 1024 * 1024); // 10MB
    
    FileMetadata large_metadata;
    large_metadata.file_path = large_file;
    large_metadata.file_size = 10 * 1024 * 1024;
    large_metadata.chunk_size = 64 * 1024;
    large_metadata.chunk_count = 160; // 10MB / 64KB
    
    FileVerifier verifier;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // This should be fast even for large files
    std::vector<uint64_t> corrupted_chunks;
    auto result = verifier.detect_corruption(large_metadata, corrupted_chunks);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_TRUE(result.success());
    EXPECT_LT(duration.count(), 1000); // Should complete within 1 second
}

// Integration tests
TEST_F(PerformanceReliabilityTest, Integration_ResumeWithVerification) {
    std::string resume_db = test_dir_ / "integration_resume.db";
    ResumeManager resume_manager(resume_db);
    FileVerifier verifier;
    
    // Create resume info with some completed chunks
    ResumeInfo info;
    info.file_id = test_metadata_.file_id;
    info.session_id = "integration_test";
    info.completed_chunks = {0, 1, 2}; // First 3 chunks
    info.stats.bytes_transferred = 3 * test_metadata_.chunk_size;
    
    resume_manager.save_resume_state(info);
    
    // Verify that completed chunks are actually valid
    std::ifstream file(test_file_path_, std::ios::binary);
    
    for (uint64_t chunk_idx : info.completed_chunks) {
        std::vector<uint8_t> chunk_data(test_metadata_.chunk_size);
        file.seekg(chunk_idx * test_metadata_.chunk_size);
        file.read(reinterpret_cast<char*>(chunk_data.data()), test_metadata_.chunk_size);
        chunk_data.resize(file.gcount());
        
        auto chunk_hash = Blake3Hasher::hash(chunk_data);
        auto verify_result = verifier.verify_chunk(chunk_data, chunk_hash);
        EXPECT_TRUE(verify_result.success());
    }
}

TEST_F(PerformanceReliabilityTest, Integration_BandwidthWithMonitoring) {
    BandwidthLimiter limiter;
    PerformanceMonitor monitor;
    
    limiter.set_max_bandwidth(512 * 1024); // 512KB/s
    
    std::string session_id = "bandwidth_monitor_test";
    monitor.start_session(session_id, 1024 * 1024); // 1MB transfer
    
    auto start = std::chrono::steady_clock::now();
    uint64_t total_transferred = 0;
    
    // Simulate transfer with bandwidth limiting
    while (total_transferred < 1024 * 1024) {
        uint32_t chunk_size = 64 * 1024; // 64KB chunks
        
        if (limiter.can_send(chunk_size)) {
            limiter.consume_tokens(chunk_size);
            monitor.on_bytes_transferred(session_id, chunk_size);
            total_transferred += chunk_size;
            
            monitor.update_statistics();
            auto stats = monitor.get_session_stats(session_id);
            
            // Speed should be limited by bandwidth limiter
            EXPECT_LT(stats.current_speed_bps, 600 * 1024); // Should be < 600KB/s
        }
        
        limiter.refill_bucket();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    
    // Should take approximately 2 seconds (1MB at 512KB/s)
    EXPECT_GE(duration.count(), 1);
    EXPECT_LE(duration.count(), 4); // Allow some tolerance
}

// Stress tests
TEST_F(PerformanceReliabilityTest, Stress_ConcurrentMonitoring) {
    PerformanceMonitor monitor;
    const int num_sessions = 100;
    
    // Start many concurrent sessions
    std::vector<std::string> session_ids;
    for (int i = 0; i < num_sessions; ++i) {
        std::string session_id = "stress_session_" + std::to_string(i);
        monitor.start_session(session_id, 1024 * 1024);
        session_ids.push_back(session_id);
    }
    
    // Update all sessions concurrently
    std::vector<std::thread> threads;
    for (const auto& session_id : session_ids) {
        threads.emplace_back([&monitor, session_id]() {
            for (int j = 0; j < 100; ++j) {
                monitor.on_bytes_transferred(session_id, 1024);
                monitor.update_statistics();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All sessions should be tracked correctly
    auto all_stats = monitor.get_all_sessions();
    EXPECT_EQ(all_stats.size(), num_sessions);
    
    for (const auto& stats : all_stats) {
        EXPECT_EQ(stats.bytes_transferred, 100 * 1024); // 100KB transferred per session
    }
}