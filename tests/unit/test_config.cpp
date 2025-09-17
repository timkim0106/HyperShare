#include <gtest/gtest.h>
#include "hypershare/core/config.hpp"
#include <fstream>
#include <filesystem>

using namespace hypershare::core;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        Config::instance().clear();
        test_file = "test_config.txt";
    }
    
    void TearDown() override {
        if (std::filesystem::exists(test_file)) {
            std::filesystem::remove(test_file);
        }
    }
    
    std::string test_file;
};

TEST_F(ConfigTest, SetAndGet) {
    auto& config = Config::instance();
    
    config.set("test.key", "test_value");
    
    auto value = config.get("test.key");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

TEST_F(ConfigTest, GetNonExistent) {
    auto& config = Config::instance();
    
    auto value = config.get("nonexistent.key");
    EXPECT_FALSE(value.has_value());
}

TEST_F(ConfigTest, GetTypedValues) {
    auto& config = Config::instance();
    
    config.set("bool.true", "true");
    config.set("bool.false", "false");
    config.set("int.value", "42");
    config.set("string.value", "hello world");
    
    EXPECT_TRUE(config.get_bool("bool.true"));
    EXPECT_FALSE(config.get_bool("bool.false"));
    EXPECT_EQ(config.get_int("int.value"), 42);
    EXPECT_EQ(config.get_string("string.value"), "hello world");
}

TEST_F(ConfigTest, DefaultValues) {
    auto& config = Config::instance();
    
    EXPECT_FALSE(config.get_bool("nonexistent", false));
    EXPECT_TRUE(config.get_bool("nonexistent", true));
    EXPECT_EQ(config.get_int("nonexistent", 123), 123);
    EXPECT_EQ(config.get_string("nonexistent", "default"), "default");
}

TEST_F(ConfigTest, LoadFromFile) {
    std::ofstream file(test_file);
    file << "# Comment line\n";
    file << "key1=value1\n";
    file << "key2 = value2 \n";
    file << "bool.setting=true\n";
    file << "int.setting=100\n";
    file.close();
    
    auto& config = Config::instance();
    EXPECT_TRUE(config.load_from_file(test_file));
    
    EXPECT_EQ(config.get_string("key1"), "value1");
    EXPECT_EQ(config.get_string("key2"), "value2");
    EXPECT_TRUE(config.get_bool("bool.setting"));
    EXPECT_EQ(config.get_int("int.setting"), 100);
}

TEST_F(ConfigTest, SaveToFile) {
    auto& config = Config::instance();
    config.set("test.key1", "value1");
    config.set("test.key2", "value2");
    
    EXPECT_TRUE(config.save_to_file(test_file));
    EXPECT_TRUE(std::filesystem::exists(test_file));
    
    Config new_config;
    EXPECT_TRUE(new_config.load_from_file(test_file));
    EXPECT_EQ(new_config.get_string("test.key1"), "value1");
    EXPECT_EQ(new_config.get_string("test.key2"), "value2");
}