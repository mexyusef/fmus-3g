#include <gtest/gtest.h>
#include <fmus/core/config.hpp>
#include <filesystem>

namespace fmus::core::test {

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        config().clear();
    }
};

TEST_F(ConfigTest, BasicTypes) {
    // Set values
    config().set("null_value", nullptr);
    config().set("bool_value", true);
    config().set("int_value", int64_t{42});
    config().set("double_value", 3.14);
    config().set("string_value", std::string("hello"));

    // Get values
    EXPECT_TRUE(config().get<std::nullptr_t>("null_value").is_ok());
    EXPECT_EQ(config().get<bool>("bool_value").value(), true);
    EXPECT_EQ(config().get<int64_t>("int_value").value(), 42);
    EXPECT_EQ(config().get<double>("double_value").value(), 3.14);
    EXPECT_EQ(config().get<std::string>("string_value").value(), "hello");
}

TEST_F(ConfigTest, ArrayType) {
    std::vector<ConfigValue> arr = {
        nullptr,
        true,
        int64_t{42},
        3.14,
        std::string("hello")
    };

    config().set("array", arr);

    auto result = config().get<std::vector<ConfigValue>>("array");
    ASSERT_TRUE(result.is_ok());

    const auto& value = result.value();
    EXPECT_EQ(value.size(), 5);
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(value[0]));
    EXPECT_EQ(std::get<bool>(value[1]), true);
    EXPECT_EQ(std::get<int64_t>(value[2]), 42);
    EXPECT_EQ(std::get<double>(value[3]), 3.14);
    EXPECT_EQ(std::get<std::string>(value[4]), "hello");
}

TEST_F(ConfigTest, NestedObject) {
    auto nested = ConfigNode::create();
    nested->set("key", "value");

    config().set("object", nested);

    auto result = config().get<ConfigNodePtr>("object");
    ASSERT_TRUE(result.is_ok());

    auto str_result = result.value()->get<std::string>("key");
    ASSERT_TRUE(str_result.is_ok());
    EXPECT_EQ(str_result.value(), "value");
}

TEST_F(ConfigTest, ErrorHandling) {
    config().set("value", 42);

    // Key not found
    EXPECT_TRUE(config().get<int>("nonexistent").is_error());

    // Type mismatch
    EXPECT_TRUE(config().get<std::string>("value").is_error());
}

TEST_F(ConfigTest, JsonSerialization) {
    // Setup test data
    config().set("null_value", nullptr);
    config().set("bool_value", true);
    config().set("int_value", int64_t{42});
    config().set("double_value", 3.14);
    config().set("string_value", std::string("hello"));

    std::vector<ConfigValue> arr = {true, int64_t{1}, "test"};
    config().set("array_value", arr);

    auto nested = ConfigNode::create();
    nested->set("nested_key", "nested_value");
    config().set("object_value", nested);

    // Serialize to string
    auto save_result = config().saveToString();
    ASSERT_TRUE(save_result.is_ok());

    // Clear and load back
    config().clear();
    auto load_result = config().loadFromString(save_result.value());
    ASSERT_TRUE(load_result.is_ok());

    // Verify values
    EXPECT_TRUE(config().get<std::nullptr_t>("null_value").is_ok());
    EXPECT_EQ(config().get<bool>("bool_value").value(), true);
    EXPECT_EQ(config().get<int64_t>("int_value").value(), 42);
    EXPECT_EQ(config().get<double>("double_value").value(), 3.14);
    EXPECT_EQ(config().get<std::string>("string_value").value(), "hello");

    auto arr_result = config().get<std::vector<ConfigValue>>("array_value");
    ASSERT_TRUE(arr_result.is_ok());
    EXPECT_EQ(arr_result.value().size(), 3);

    auto obj_result = config().get<ConfigNodePtr>("object_value");
    ASSERT_TRUE(obj_result.is_ok());
    EXPECT_EQ(obj_result.value()->get<std::string>("nested_key").value(), "nested_value");
}

TEST_F(ConfigTest, FileOperations) {
    // Setup test data
    config().set("test_value", "test");

    // Save to file
    auto temp_path = std::filesystem::temp_directory_path() / "config_test.json";
    auto save_result = config().saveToFile(temp_path);
    ASSERT_TRUE(save_result.is_ok());

    // Clear and load back
    config().clear();
    auto load_result = config().loadFromFile(temp_path);
    ASSERT_TRUE(load_result.is_ok());

    // Verify value
    EXPECT_EQ(config().get<std::string>("test_value").value(), "test");

    // Cleanup
    std::filesystem::remove(temp_path);
}

TEST_F(ConfigTest, InvalidJson) {
    // Invalid JSON string
    auto result = config().loadFromString("invalid json");
    EXPECT_TRUE(result.is_error());

    // Valid JSON but not an object
    result = config().loadFromString("42");
    EXPECT_TRUE(result.is_error());
}

TEST_F(ConfigTest, FileErrors) {
    // Nonexistent file
    auto result = config().loadFromFile("nonexistent.json");
    EXPECT_TRUE(result.is_error());

    // Invalid path for saving
    result = config().saveToFile("/invalid/path/config.json");
    EXPECT_TRUE(result.is_error());
}

} // namespace fmus::core::test