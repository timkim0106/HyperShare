#include <gtest/gtest.h>
#include "hypershare/core/logger.hpp"
#include <filesystem>
#include <fstream>

using namespace hypershare::core;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file = "test_hypershare.log";
    }
    
    void TearDown() override {
        Logger::shutdown();
        if (std::filesystem::exists(log_file)) {
            std::filesystem::remove(log_file);
        }
    }
    
    std::string log_file;
};

TEST_F(LoggerTest, Initialize) {
    Logger::initialize(log_file, LogLevel::Debug);
    
    EXPECT_NE(Logger::get(), nullptr);
    EXPECT_TRUE(std::filesystem::exists(log_file));
}

TEST_F(LoggerTest, LogMessages) {
    Logger::initialize(log_file, LogLevel::Debug);
    
    LOG_DEBUG("Debug message: {}", 123);
    LOG_INFO("Info message: {}", "test");
    LOG_WARN("Warning message");
    LOG_ERROR("Error message");
    LOG_CRITICAL("Critical message");
    
    Logger::get()->flush();
    
    std::ifstream file(log_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    EXPECT_TRUE(content.find("Debug message: 123") != std::string::npos);
    EXPECT_TRUE(content.find("Info message: test") != std::string::npos);
    EXPECT_TRUE(content.find("Warning message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
    EXPECT_TRUE(content.find("Critical message") != std::string::npos);
}

TEST_F(LoggerTest, LogLevel) {
    Logger::initialize(log_file, LogLevel::Warn);
    
    LOG_DEBUG("Debug message");
    LOG_INFO("Info message");
    LOG_WARN("Warning message");
    
    Logger::get()->flush();
    
    std::ifstream file(log_file);
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    
    EXPECT_TRUE(content.find("Debug message") == std::string::npos);
    EXPECT_TRUE(content.find("Info message") == std::string::npos);
    EXPECT_TRUE(content.find("Warning message") != std::string::npos);
}