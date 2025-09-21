#include <gtest/gtest.h>
#include "hypershare/storage/file_metadata.hpp"
#include "hypershare/storage/chunk_manager.hpp"
#include "hypershare/storage/file_index.hpp"
#include "hypershare/storage/storage_config.hpp"
#include "hypershare/crypto/hash.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <atomic>

using namespace hypershare::storage;
using namespace hypershare::crypto;

class FileStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir_ = std::filesystem::temp_directory_path() / "hypershare_test";
        std::filesystem::create_directories(test_dir_);
        
        // Create test config
        config_.download_directory = test_dir_ / "downloads";
        config_.incomplete_directory = test_dir_ / "incomplete";
        config_.database_path = test_dir_ / "test.db";
        
        std::filesystem::create_directories(config_.download_directory);
        std::filesystem::create_directories(config_.incomplete_directory);
        
        // Create test files
        create_test_file("small_file.txt", 1024);        // 1KB
        create_test_file("medium_file.txt", 65536 * 3);  // 3 chunks (192KB)
        create_test_file("large_file.txt", 1024 * 1024); // 1MB
    }
    
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    
    void create_test_file(const std::string& filename, size_t size) {
        std::filesystem::path file_path = test_dir_ / filename;
        std::ofstream file(file_path, std::ios::binary);
        
        // Create deterministic content for testing
        std::mt19937 rng(42); // Fixed seed for reproducible tests
        std::uniform_int_distribution<uint8_t> dist(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            file.put(static_cast<char>(dist(rng)));
        }
        file.close();
        
        test_files_[filename] = file_path;
    }
    
    std::filesystem::path test_dir_;
    StorageConfig config_;
    std::map<std::string, std::filesystem::path> test_files_;
};

// Test FileMetadata functionality
TEST_F(FileStorageTest, FileMetadata_BasicOperations) {
    FileMetadata metadata;
    metadata.file_id = "test_file_123";
    metadata.filename = "test.txt";
    metadata.file_path = "/path/to/test.txt";
    metadata.file_size = 65536;
    metadata.chunk_size = 16384;
    metadata.chunk_count = 4;
    
    // Test calculated properties
    EXPECT_EQ(metadata.get_chunk_count(), 4);
    EXPECT_EQ(metadata.get_chunk_size(0), 16384);
    EXPECT_EQ(metadata.get_chunk_size(3), 16384);
    
    // Test serialization
    auto serialized = metadata.serialize();
    auto deserialized = FileMetadata::deserialize(serialized);
    
    EXPECT_EQ(metadata.file_id, deserialized.file_id);
    EXPECT_EQ(metadata.filename, deserialized.filename);
    EXPECT_EQ(metadata.file_size, deserialized.file_size);
    EXPECT_EQ(metadata.chunk_size, deserialized.chunk_size);
}

TEST_F(FileStorageTest, FileMetadata_ChunkCalculations) {
    FileMetadata metadata;
    metadata.file_size = 100000;  // 100KB
    metadata.chunk_size = 32768;  // 32KB chunks
    
    // Should have 4 chunks: 32KB + 32KB + 32KB + 4KB
    EXPECT_EQ(metadata.get_chunk_count(), 4);
    EXPECT_EQ(metadata.get_chunk_size(0), 32768);
    EXPECT_EQ(metadata.get_chunk_size(1), 32768);
    EXPECT_EQ(metadata.get_chunk_size(2), 32768);
    EXPECT_EQ(metadata.get_chunk_size(3), 4464);  // Remainder
}

// Test ChunkManager functionality
TEST_F(FileStorageTest, ChunkManager_FileChunking) {
    ChunkManager chunk_manager(config_);
    
    auto file_path = test_files_["medium_file.txt"];
    FileMetadata metadata;
    
    // Test chunking
    auto result = chunk_manager.chunk_file(file_path.string(), metadata);
    ASSERT_TRUE(result.success()) << result.message;
    
    EXPECT_EQ(metadata.file_size, 65536 * 3);
    EXPECT_EQ(metadata.chunk_size, ChunkManager::DEFAULT_CHUNK_SIZE);
    EXPECT_EQ(metadata.chunk_count, 3);
    EXPECT_EQ(metadata.chunk_hashes.size(), 3);
    
    // Verify each chunk hash is valid (not empty)
    for (const auto& hash : metadata.chunk_hashes) {
        EXPECT_FALSE(std::all_of(hash.begin(), hash.end(), [](uint8_t b) { return b == 0; }));
    }
}

TEST_F(FileStorageTest, ChunkManager_ReadWriteChunks) {
    ChunkManager chunk_manager(config_);
    
    auto file_path = test_files_["medium_file.txt"];
    FileMetadata metadata;
    chunk_manager.chunk_file(file_path.string(), metadata);
    
    // Read each chunk and verify
    for (uint64_t i = 0; i < metadata.chunk_count; ++i) {
        std::vector<uint8_t> chunk_data;
        auto result = chunk_manager.read_chunk(metadata, i, chunk_data);
        ASSERT_TRUE(result.success()) << "Failed to read chunk " << i;
        
        // Verify chunk size
        if (i < metadata.chunk_count - 1) {
            EXPECT_EQ(chunk_data.size(), metadata.chunk_size);
        } else {
            // Last chunk might be smaller
            EXPECT_LE(chunk_data.size(), metadata.chunk_size);
        }
        
        // Verify chunk hash
        EXPECT_TRUE(chunk_manager.verify_chunk_hash(chunk_data, metadata.chunk_hashes[i]));
    }
}

TEST_F(FileStorageTest, ChunkManager_IncompleteFileHandling) {
    ChunkManager chunk_manager(config_);
    
    // Create metadata for incomplete file
    FileMetadata metadata;
    metadata.file_id = "incomplete_test";
    metadata.filename = "incomplete.txt";
    metadata.file_path = config_.incomplete_directory / "incomplete.txt";
    metadata.file_size = 100000;
    metadata.chunk_size = 32768;
    metadata.chunk_count = metadata.get_chunk_count();
    metadata.chunk_hashes.resize(metadata.chunk_count);
    
    // Write some chunks (simulate partial download)
    std::vector<uint8_t> test_chunk(32768, 0x42);
    
    auto result = chunk_manager.write_chunk(metadata, 0, test_chunk);
    EXPECT_TRUE(result.success());
    
    result = chunk_manager.write_chunk(metadata, 2, test_chunk);
    EXPECT_TRUE(result.success());
    
    // Verify partial file exists
    EXPECT_TRUE(std::filesystem::exists(metadata.file_path));
    
    // Read back written chunks
    std::vector<uint8_t> read_chunk;
    result = chunk_manager.read_chunk(metadata, 0, read_chunk);
    EXPECT_TRUE(result.success());
    EXPECT_EQ(read_chunk, test_chunk);
}

TEST_F(FileStorageTest, ChunkManager_HashVerification) {
    ChunkManager chunk_manager(config_);
    
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};
    auto hash = Blake3Hasher::hash(test_data);
    auto hash_str = hypershare::crypto::hash_utils::hash_to_hex(hash);
    
    // Correct data should verify
    EXPECT_TRUE(chunk_manager.verify_chunk_hash(test_data, hash_str));
    
    // Modified data should not verify
    std::vector<uint8_t> modified_data = {0x01, 0x02, 0x03, 0x05};
    EXPECT_FALSE(chunk_manager.verify_chunk_hash(modified_data, hash_str));
    
    // Empty data should not verify
    std::vector<uint8_t> empty_data;
    EXPECT_FALSE(chunk_manager.verify_chunk_hash(empty_data, hash_str));
}

// Test FileIndex functionality
TEST_F(FileStorageTest, FileIndex_BasicOperations) {
    FileIndex file_index(config_.database_path);
    
    // Create test metadata
    FileMetadata metadata;
    metadata.file_id = "test_file_456";
    metadata.filename = "index_test.txt";
    metadata.file_path = "/path/to/index_test.txt";
    metadata.file_size = 65536;
    metadata.chunk_size = 16384;
    metadata.chunk_count = 4;
    metadata.tags = {"document", "test"};
    
    // Add file to index
    auto add_result = file_index.add_file(metadata);
    EXPECT_TRUE(add_result);
    
    // Retrieve file from index
    FileMetadata retrieved;
    auto get_result = file_index.get_file(metadata.file_id, retrieved);
    EXPECT_TRUE(get_result.success());
    
    EXPECT_EQ(metadata.file_id, retrieved.file_id);
    EXPECT_EQ(metadata.filename, retrieved.filename);
    EXPECT_EQ(metadata.file_size, retrieved.file_size);
    EXPECT_EQ(metadata.tags, retrieved.tags);
    
    // Remove file from index
    auto remove_result = file_index.remove_file(metadata.file_id);
    EXPECT_TRUE(remove_result.success());
    
    // Should not be able to retrieve removed file
    get_result = file_index.get_file(metadata.file_id, retrieved);
    EXPECT_FALSE(get_result.success());
}

TEST_F(FileStorageTest, FileIndex_SearchFiles) {
    FileIndex file_index(config_.database_path);
    
    // Add multiple files
    std::vector<FileMetadata> test_files;
    
    FileMetadata file1("file1", "document.pdf", 1000);
    file1.file_path = "/path/doc.pdf";
    file1.chunk_size = 1024;
    file1.chunk_count = 1;
    file1.tags = {"document", "pdf"};
    
    FileMetadata file2("file2", "image.jpg", 2000);
    file2.file_path = "/path/img.jpg";
    file2.chunk_size = 1024;
    file2.chunk_count = 2;
    file2.tags = {"image", "photo"};
    
    FileMetadata file3("file3", "text.txt", 3000);
    file3.file_path = "/path/text.txt";
    file3.chunk_size = 1024;
    file3.chunk_count = 3;
    file3.tags = {"document", "text"};
    
    test_files = {file1, file2, file3};
    
    for (const auto& metadata : test_files) {
        file_index.add_file(metadata);
    }
    
    // Search by filename
    auto results = file_index.search_files("document");
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].filename, "document.pdf");
    
    // Search by tag
    results = file_index.search_files("document");
    EXPECT_GE(results.size(), 2); // Should find both document files
    
    // Search with no results
    results = file_index.search_files("nonexistent");
    EXPECT_EQ(results.size(), 0);
}

TEST_F(FileStorageTest, FileIndex_ChunkTracking) {
    FileIndex file_index(config_.database_path);
    
    FileMetadata metadata;
    metadata.file_id = "chunk_test";
    metadata.filename = "chunk_test.txt";
    metadata.file_size = 100000;
    metadata.chunk_size = 32768;
    metadata.chunk_count = 4;
    
    file_index.add_file(metadata);
    
    // Initially, all chunks should be missing
    auto missing = file_index.get_missing_chunks(metadata.file_id);
    EXPECT_EQ(missing.size(), 4);
    
    // Mark some chunks as available
    file_index.update_chunk_progress(metadata.file_id, 0, "chunk0_hash");
    file_index.update_chunk_progress(metadata.file_id, 2, "chunk2_hash");
    
    // Check missing chunks
    missing = file_index.get_missing_chunks(metadata.file_id);
    EXPECT_EQ(missing.size(), 2);
    EXPECT_TRUE(std::find(missing.begin(), missing.end(), 1) != missing.end());
    EXPECT_TRUE(std::find(missing.begin(), missing.end(), 3) != missing.end());
    
    // Mark all chunks as available
    file_index.update_chunk_progress(metadata.file_id, 1, "chunk1_hash");
    file_index.update_chunk_progress(metadata.file_id, 3, "chunk3_hash");
    
    missing = file_index.get_missing_chunks(metadata.file_id);
    EXPECT_EQ(missing.size(), 0);
}

// Test StorageConfig functionality
TEST_F(FileStorageTest, StorageConfig_DefaultValues) {
    StorageConfig config("./");
    
    EXPECT_EQ(config.download_directory, "./downloads");
    EXPECT_EQ(config.incomplete_directory, "./incomplete");
    EXPECT_EQ(config.database_path, "./hypershare.db");
    EXPECT_EQ(config.default_chunk_size, 65536U);
    EXPECT_EQ(config.max_storage_size, 10ULL * 1024 * 1024 * 1024);
}

TEST_F(FileStorageTest, StorageConfig_BasicOperations) {
    StorageConfig config;
    config.download_directory = "/custom/downloads";
    config.incomplete_directory = "/custom/incomplete";
    config.database_path = "/custom/db.sqlite";
    config.default_chunk_size = 32768;
    
    EXPECT_TRUE(config.validate());
    EXPECT_EQ(config.download_directory, "/custom/downloads");
    EXPECT_EQ(config.incomplete_directory, "/custom/incomplete");
    EXPECT_EQ(config.database_path, "/custom/db.sqlite");
    EXPECT_EQ(config.default_chunk_size, 32768U);
}

// Test error conditions
TEST_F(FileStorageTest, ErrorHandling_InvalidFiles) {
    ChunkManager chunk_manager(config_);
    
    FileMetadata metadata;
    auto result = chunk_manager.chunk_file("/nonexistent/file.txt", metadata);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::FILE_NOT_FOUND);
}

TEST_F(FileStorageTest, ErrorHandling_InvalidChunkIndex) {
    ChunkManager chunk_manager(config_);
    
    FileMetadata metadata;
    metadata.chunk_count = 5;
    
    std::vector<uint8_t> chunk_data;
    auto result = chunk_manager.read_chunk(metadata, 10, chunk_data);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::INVALID_STATE);
}

TEST_F(FileStorageTest, ErrorHandling_DatabaseErrors) {
    // Create file index with invalid path
    FileIndex file_index("/invalid/path/database.db");
    
    FileMetadata metadata;
    metadata.file_id = "test";
    
    auto result = file_index.add_file(metadata);
    EXPECT_FALSE(result);
}

// Test large file handling
TEST_F(FileStorageTest, LargeFile_ChunkingPerformance) {
    ChunkManager chunk_manager(config_);
    
    auto file_path = test_files_["large_file.txt"];
    FileMetadata metadata;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto result = chunk_manager.chunk_file(file_path.string(), metadata);
    auto end = std::chrono::high_resolution_clock::now();
    
    ASSERT_TRUE(result.success());
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 1000); // Should complete within 1 second
    
    // Verify chunking results
    EXPECT_EQ(metadata.file_size, 1024 * 1024);
    EXPECT_EQ(metadata.chunk_count, 16); // 1MB / 64KB = 16 chunks
    EXPECT_EQ(metadata.chunk_hashes.size(), 16);
}

// Test concurrent operations
TEST_F(FileStorageTest, Concurrent_FileOperations) {
    FileIndex file_index(config_.database_path);
    
    // Add multiple files concurrently
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&file_index, &success_count, i]() {
            FileMetadata metadata;
            metadata.file_id = "concurrent_" + std::to_string(i);
            metadata.filename = "file_" + std::to_string(i) + ".txt";
            metadata.file_size = 1000 * i;
            
            if (file_index.add_file(metadata)) {
                success_count++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(success_count.load(), 10);
}