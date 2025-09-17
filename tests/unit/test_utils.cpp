#include <gtest/gtest.h>
#include "hypershare/core/utils.hpp"
#include <filesystem>
#include <fstream>

using namespace hypershare::core::utils;

class StringUtilsTest : public ::testing::Test {};

TEST_F(StringUtilsTest, Split) {
    auto result = StringUtils::split("a,b,c", ',');
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
    
    auto empty = StringUtils::split("", ',');
    EXPECT_EQ(empty.size(), 1);
    EXPECT_EQ(empty[0], "");
}

TEST_F(StringUtilsTest, Join) {
    std::vector<std::string> parts = {"a", "b", "c"};
    auto result = StringUtils::join(parts, ",");
    EXPECT_EQ(result, "a,b,c");
    
    std::vector<std::string> empty;
    auto empty_result = StringUtils::join(empty, ",");
    EXPECT_EQ(empty_result, "");
}

TEST_F(StringUtilsTest, Trim) {
    EXPECT_EQ(StringUtils::trim("  hello  "), "hello");
    EXPECT_EQ(StringUtils::trim("hello"), "hello");
    EXPECT_EQ(StringUtils::trim("   "), "");
    EXPECT_EQ(StringUtils::trim(""), "");
}

TEST_F(StringUtilsTest, CaseConversion) {
    EXPECT_EQ(StringUtils::to_lower("Hello World"), "hello world");
    EXPECT_EQ(StringUtils::to_upper("Hello World"), "HELLO WORLD");
}

TEST_F(StringUtilsTest, StartsWith) {
    EXPECT_TRUE(StringUtils::starts_with("hello world", "hello"));
    EXPECT_FALSE(StringUtils::starts_with("hello world", "world"));
    EXPECT_TRUE(StringUtils::starts_with("test", "test"));
    EXPECT_FALSE(StringUtils::starts_with("test", "testing"));
}

TEST_F(StringUtilsTest, EndsWith) {
    EXPECT_TRUE(StringUtils::ends_with("hello world", "world"));
    EXPECT_FALSE(StringUtils::ends_with("hello world", "hello"));
    EXPECT_TRUE(StringUtils::ends_with("test", "test"));
    EXPECT_FALSE(StringUtils::ends_with("test", "testing"));
}

TEST_F(StringUtilsTest, FormatBytes) {
    EXPECT_EQ(StringUtils::format_bytes(1024), "1.00 KB");
    EXPECT_EQ(StringUtils::format_bytes(1048576), "1.00 MB");
    EXPECT_EQ(StringUtils::format_bytes(500), "500.00 B");
}

class FileUtilsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file = "test_file.txt";
        test_dir = "test_dir";
    }
    
    void TearDown() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
        if (std::filesystem::exists(test_dir)) {
            std::filesystem::remove_all(test_dir);
        }
    }
    
    std::string test_file;
    std::string test_dir;
};

TEST_F(FileUtilsTest, Exists) {
    EXPECT_FALSE(FileUtils::exists(test_file));
    
    std::ofstream file(test_file);
    file << "test";
    file.close();
    
    EXPECT_TRUE(FileUtils::exists(test_file));
}

TEST_F(FileUtilsTest, IsFile) {
    std::ofstream file(test_file);
    file << "test";
    file.close();
    
    EXPECT_TRUE(FileUtils::is_file(test_file));
    EXPECT_FALSE(FileUtils::is_directory(test_file));
}

TEST_F(FileUtilsTest, CreateDirectories) {
    EXPECT_TRUE(FileUtils::create_directories(test_dir));
    EXPECT_TRUE(FileUtils::exists(test_dir));
    EXPECT_TRUE(FileUtils::is_directory(test_dir));
}

TEST_F(FileUtilsTest, ReadWriteFile) {
    std::string content = "Hello, World!";
    
    EXPECT_TRUE(FileUtils::write_file(test_file, content));
    
    auto read_content = FileUtils::read_file(test_file);
    ASSERT_TRUE(read_content.has_value());
    EXPECT_EQ(*read_content, content);
}

TEST_F(FileUtilsTest, FileSize) {
    std::string content = "Hello, World!";
    FileUtils::write_file(test_file, content);
    
    auto size = FileUtils::file_size(test_file);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, content.length());
}

TEST_F(FileUtilsTest, GetFileExtension) {
    EXPECT_EQ(FileUtils::get_file_extension("test.txt"), ".txt");
    EXPECT_EQ(FileUtils::get_file_extension("test.tar.gz"), ".gz");
    EXPECT_EQ(FileUtils::get_file_extension("test"), "");
}

class TimeUtilsTest : public ::testing::Test {};

TEST_F(TimeUtilsTest, Now) {
    auto time1 = TimeUtils::now();
    auto time2 = TimeUtils::now();
    
    EXPECT_GE(time2, time1);
}

TEST_F(TimeUtilsTest, FormatTimestamp) {
    auto now = TimeUtils::now();
    auto timestamp = TimeUtils::format_timestamp(now);
    
    EXPECT_FALSE(timestamp.empty());
    EXPECT_TRUE(timestamp.find('-') != std::string::npos);
    EXPECT_TRUE(timestamp.find(':') != std::string::npos);
}