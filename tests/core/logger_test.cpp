#include <gtest/gtest.h>
#include <fmus/core/logger.hpp>
#include <sstream>
#include <filesystem>

namespace fmus::core::test {

// Test sink yang menyimpan pesan ke string untuk testing
class TestSink : public ILogSink {
public:
    void write(const LogMessage& msg) override {
        messages_.push_back(std::string(msg.message));
    }

    const std::vector<std::string>& messages() const { return messages_; }
    void clear() { messages_.clear(); }

private:
    std::vector<std::string> messages_;
};

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testSink_ = std::make_shared<TestSink>();
        logger().addSink(testSink_);
    }

    void TearDown() override {
        testSink_->clear();
    }

    std::shared_ptr<TestSink> testSink_;
};

TEST_F(LoggerTest, BasicLogging) {
    logger().info("Test message");
    ASSERT_EQ(testSink_->messages().size(), 1);
    EXPECT_EQ(testSink_->messages()[0], "Test message");
}

TEST_F(LoggerTest, LogLevels) {
    logger().setLevel(LogLevel::Warning);

    logger().trace("Trace message");
    logger().debug("Debug message");
    logger().info("Info message");
    logger().warning("Warning message");
    logger().error("Error message");

    ASSERT_EQ(testSink_->messages().size(), 2);
    EXPECT_EQ(testSink_->messages()[0], "Warning message");
    EXPECT_EQ(testSink_->messages()[1], "Error message");
}

TEST_F(LoggerTest, FormatString) {
    logger().info("Value: {}, String: {}", 42, "test");
    ASSERT_EQ(testSink_->messages().size(), 1);
    EXPECT_EQ(testSink_->messages()[0], "Value: 42, String: test");
}

TEST_F(LoggerTest, FileSink) {
    const std::string testLogFile = "test.log";
    auto fileSink = std::make_shared<FileSink>(testLogFile);
    logger().addSink(fileSink);

    logger().info("File test message");

    // Verifikasi file dibuat dan berisi pesan
    EXPECT_TRUE(std::filesystem::exists(testLogFile));

    // Bersihkan file test
    std::filesystem::remove(testLogFile);
}

TEST_F(LoggerTest, MultipleMessages) {
    for (int i = 0; i < 5; ++i) {
        logger().info("Message {}", i);
    }

    ASSERT_EQ(testSink_->messages().size(), 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(testSink_->messages()[i], std::format("Message {}", i));
    }
}

TEST_F(LoggerTest, ThreadSafety) {
    constexpr int numThreads = 10;
    constexpr int messagesPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < messagesPerThread; ++j) {
                logger().info("Thread {} Message {}", i, j);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(testSink_->messages().size(), numThreads * messagesPerThread);
}

} // namespace fmus::core::test