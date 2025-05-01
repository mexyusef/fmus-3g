#include <gtest/gtest.h>
#include <fmus/core/buffer.hpp>
#include <array>
#include <string>
#include <algorithm>
#include <random>

namespace fmus::core::test {

TEST(BufferViewTest, DefaultConstruction) {
    BufferView<int> view;
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.size(), 0);
    EXPECT_EQ(view.data(), nullptr);
}

TEST(BufferViewTest, ArrayConstruction) {
    int arr[] = {1, 2, 3, 4, 5};
    BufferView<int> view(arr);

    EXPECT_EQ(view.size(), 5);
    EXPECT_FALSE(view.empty());
    EXPECT_EQ(view.data(), arr);

    for (size_t i = 0; i < view.size(); ++i) {
        EXPECT_EQ(view[i], arr[i]);
    }
}

TEST(BufferViewTest, SpanConstruction) {
    std::array<int, 5> arr = {1, 2, 3, 4, 5};
    std::span<int> span(arr);
    BufferView<int> view(span);

    EXPECT_EQ(view.size(), 5);
    EXPECT_EQ(view.data(), arr.data());
}

TEST(BufferViewTest, VectorConstruction) {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    BufferView<int> view(vec);

    EXPECT_EQ(view.size(), vec.size());
    EXPECT_EQ(view.data(), vec.data());
}

TEST(BufferViewTest, Subview) {
    int arr[] = {1, 2, 3, 4, 5};
    BufferView<int> view(arr);

    auto sub = view.subview(1, 3);
    EXPECT_EQ(sub.size(), 3);
    EXPECT_EQ(sub[0], 2);
    EXPECT_EQ(sub[1], 3);
    EXPECT_EQ(sub[2], 4);
}

TEST(BufferViewTest, InvalidSubview) {
    int arr[] = {1, 2, 3};
    BufferView<int> view(arr);

    EXPECT_THROW(view.subview(4, 1), Error);
    EXPECT_THROW(view.subview(1, 3), Error);
}

TEST(BufferTest, DefaultConstruction) {
    Buffer<int> buffer;
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0);
}

TEST(BufferTest, SizeConstruction) {
    Buffer<int> buffer(5);
    EXPECT_EQ(buffer.size(), 5);
    EXPECT_FALSE(buffer.empty());
}

TEST(BufferTest, DataConstruction) {
    int arr[] = {1, 2, 3, 4, 5};
    Buffer<int> buffer(arr, 5);

    EXPECT_EQ(buffer.size(), 5);
    for (size_t i = 0; i < buffer.size(); ++i) {
        EXPECT_EQ(buffer[i], arr[i]);
    }
}

TEST(BufferTest, ViewConstruction) {
    int arr[] = {1, 2, 3, 4, 5};
    BufferView<int> view(arr);
    Buffer<int> buffer(view);

    EXPECT_EQ(buffer.size(), 5);
    for (size_t i = 0; i < buffer.size(); ++i) {
        EXPECT_EQ(buffer[i], arr[i]);
    }
}

TEST(BufferTest, Append) {
    Buffer<int> buffer;

    // Append array
    int arr[] = {1, 2, 3};
    buffer.append(arr, 3);
    EXPECT_EQ(buffer.size(), 3);

    // Append view
    BufferView<int> view(arr);
    buffer.append(view);
    EXPECT_EQ(buffer.size(), 6);

    // Append buffer
    Buffer<int> other(arr, 3);
    buffer.append(other);
    EXPECT_EQ(buffer.size(), 9);

    // Verify content
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(buffer[i], arr[i]);
        EXPECT_EQ(buffer[i + 3], arr[i]);
        EXPECT_EQ(buffer[i + 6], arr[i]);
    }
}

TEST(BufferTest, ViewConversion) {
    Buffer<int> buffer = {1, 2, 3, 4, 5};
    BufferView<int> view = buffer.view();

    EXPECT_EQ(view.size(), buffer.size());
    EXPECT_EQ(view.data(), buffer.data());
}

TEST(BufferTest, ByteBufferStringConversion) {
    std::string str = "Hello, World!";

    // String to bytes
    ByteBufferView bytes = as_bytes(str);
    EXPECT_EQ(bytes.size(), str.size());
    EXPECT_EQ(memcmp(bytes.data(), str.data(), str.size()), 0);

    // Bytes to string
    std::string_view str_view = as_string(bytes);
    EXPECT_EQ(str_view, str);
}

TEST(BufferTest, MoveSemantics) {
    Buffer<std::unique_ptr<int>> buffer;
    buffer.resize(1);
    buffer[0] = std::make_unique<int>(42);

    Buffer<std::unique_ptr<int>> moved = std::move(buffer);
    EXPECT_EQ(*moved[0], 42);
    EXPECT_TRUE(buffer.empty());
}

class BufferTest : public ::testing::Test {
protected:
    static constexpr std::size_t kTestSize = 1024;

    void SetUp() override {
        // Generate test data
        test_data_.resize(kTestSize);
        std::iota(test_data_.begin(), test_data_.end(), 0);
    }

    std::vector<std::byte> test_data_;
};

// Test basic buffer operations
TEST_F(BufferTest, BasicOperations) {
    Buffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0);

    // Test append
    buf.append(test_data_.data(), test_data_.size());
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), test_data_.size());

    // Verify data
    auto view = buf.view();
    EXPECT_TRUE(std::equal(view.begin(), view.end(), test_data_.begin()));

    // Test clear
    buf.clear();
    EXPECT_TRUE(buf.empty());
}

// Test capacity management
TEST_F(BufferTest, CapacityManagement) {
    Buffer buf(64);  // Initial capacity
    EXPECT_GE(buf.capacity(), 64);

    // Test growth
    buf.append(test_data_.data(), 32);
    std::size_t initial_capacity = buf.capacity();

    buf.append(test_data_.data(), 64);
    EXPECT_GT(buf.capacity(), initial_capacity);

    // Test reserve
    buf.reserve(256);
    EXPECT_GE(buf.capacity(), 256);

    // Test resize
    buf.resize(128);
    EXPECT_EQ(buf.size(), 128);
}

// Test string operations
TEST_F(BufferTest, StringOperations) {
    Buffer buf;
    std::string test_str = "Hello, World!";

    // Test string append
    buf.append(test_str);
    EXPECT_EQ(buf.size(), test_str.size());

    // Test string view
    auto str_view = buf.string_view();
    EXPECT_EQ(str_view, test_str);
}

// Test copy and move
TEST_F(BufferTest, CopyAndMove) {
    Buffer buf1;
    buf1.append(test_data_.data(), test_data_.size());

    // Test copy constructor
    Buffer buf2(buf1);
    EXPECT_EQ(buf2.size(), buf1.size());
    EXPECT_TRUE(std::equal(
        buf2.view().begin(), buf2.view().end(),
        buf1.view().begin()));

    // Test move constructor
    Buffer buf3(std::move(buf2));
    EXPECT_EQ(buf3.size(), buf1.size());
    EXPECT_TRUE(buf2.empty());  // NOLINT: use-after-move intended for testing

    // Test copy assignment
    Buffer buf4;
    buf4 = buf1;
    EXPECT_EQ(buf4.size(), buf1.size());

    // Test move assignment
    Buffer buf5;
    buf5 = std::move(buf3);
    EXPECT_EQ(buf5.size(), buf1.size());
    EXPECT_TRUE(buf3.empty());  // NOLINT: use-after-move intended for testing
}

// Test consume operation
TEST_F(BufferTest, ConsumeOperation) {
    Buffer buf;
    buf.append(test_data_.data(), test_data_.size());

    // Consume partial
    std::size_t consume_size = test_data_.size() / 2;
    buf.consume(consume_size);
    EXPECT_EQ(buf.size(), test_data_.size() - consume_size);

    // Verify remaining data
    auto view = buf.view();
    EXPECT_TRUE(std::equal(
        view.begin(), view.end(),
        test_data_.begin() + consume_size));

    // Consume all
    buf.consume(buf.size());
    EXPECT_TRUE(buf.empty());
}

// Test edge cases
TEST_F(BufferTest, EdgeCases) {
    Buffer buf;

    // Empty append
    buf.append(nullptr, 0);
    EXPECT_TRUE(buf.empty());

    // Null data
    EXPECT_THROW(buf.append(nullptr, 1), Error);

    // Zero-size operations
    buf.resize(0);
    EXPECT_TRUE(buf.empty());

    buf.reserve(0);
    EXPECT_EQ(buf.capacity(), 0);

    // Large allocation
    constexpr std::size_t large_size = 1024 * 1024;  // 1MB
    buf.reserve(large_size);
    EXPECT_GE(buf.capacity(), large_size);
}

// Test pool buffer
class PoolBufferTest : public ::testing::Test {
protected:
    static constexpr std::size_t kBlockSize = 1024;
    static constexpr std::size_t kInitialBlocks = 16;

    void SetUp() override {
        pool_ = std::make_unique<MemoryPool>(kBlockSize, kInitialBlocks);
    }

    std::unique_ptr<MemoryPool> pool_;
};

TEST_F(PoolBufferTest, BasicOperations) {
    PoolBuffer buf(*pool_);
    EXPECT_TRUE(buf.empty());

    // Allocate
    buf.allocate(512);
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 512);

    // Release
    buf.release();
    EXPECT_TRUE(buf.empty());
}

TEST_F(PoolBufferTest, MoveOperations) {
    PoolBuffer buf1(*pool_);
    buf1.allocate(512);

    // Move constructor
    PoolBuffer buf2(std::move(buf1));
    EXPECT_TRUE(buf1.empty());  // NOLINT: use-after-move intended for testing
    EXPECT_FALSE(buf2.empty());

    // Move assignment
    PoolBuffer buf3(*pool_);
    buf3 = std::move(buf2);
    EXPECT_TRUE(buf2.empty());  // NOLINT: use-after-move intended for testing
    EXPECT_FALSE(buf3.empty());
}

// Performance test
TEST_F(BufferTest, Performance) {
    constexpr int kIterations = 10000;
    constexpr std::size_t kChunkSize = 1024;

    Buffer buf;
    std::vector<std::byte> chunk(kChunkSize);

    // Generate random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::generate(chunk.begin(), chunk.end(),
        [&]() { return static_cast<std::byte>(dis(gen)); });

    // Test append performance
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < kIterations; ++i) {
        buf.append(chunk.data(), chunk.size());
        if (buf.size() > 1024 * 1024) {  // 1MB limit
            buf.clear();
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    // Log performance metrics
    double throughput = (static_cast<double>(kIterations) * kChunkSize) /
                       (1024.0 * 1024.0 * duration / 1000.0);  // MB/s

    logger().info("Buffer performance test: {} iterations in {} ms ({:.2f} MB/s)",
        kIterations, duration, throughput);
}

} // namespace fmus::core::test