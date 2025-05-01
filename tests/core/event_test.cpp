#include <gtest/gtest.h>
#include <fmus/core/event.hpp>
#include <thread>
#include <atomic>

namespace fmus::core::test {

class EventTest : public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = make_event_loop();
        emitter_ = make_event_emitter(*loop_);
        loop_->start();
    }

    void TearDown() override {
        loop_->stop();
        emitter_.reset();
        loop_.reset();
    }

    std::unique_ptr<EventLoop> loop_;
    std::unique_ptr<EventEmitter> emitter_;
};

// Test basic event emission dan handling
TEST_F(EventTest, BasicEventHandling) {
    std::atomic<int> counter{0};

    // Add listener
    emitter_->addListener("test", [&](const Event& e) {
        counter++;
        EXPECT_EQ(e.type(), "test");
        EXPECT_EQ(e.get<int>(), 42);
    });

    // Emit event
    emitter_->emit("test", 42);

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 1);
}

// Test multiple listeners
TEST_F(EventTest, MultipleListeners) {
    std::atomic<int> counter1{0};
    std::atomic<int> counter2{0};

    // Add listeners
    emitter_->addListener("test", [&](const Event&) { counter1++; });
    emitter_->addListener("test", [&](const Event&) { counter2++; });

    // Emit events
    emitter_->emit("test");
    emitter_->emit("test");

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter1, 2);
    EXPECT_EQ(counter2, 2);
}

// Test event filtering
TEST_F(EventTest, EventFiltering) {
    std::atomic<int> counter{0};

    // Add filtered listener
    emitter_->addListener(
        [&](const Event&) { counter++; },
        [](const Event& e) { return e.type() == "test"; }
    );

    // Emit events
    emitter_->emit("test");
    emitter_->emit("other");

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 1);
}

// Test async event handling
TEST_F(EventTest, AsyncEventHandling) {
    // Emit async event
    auto future = emitter_->emitAsync<int>(Event("async", 42));

    // Add response handler
    emitter_->emit("async", 42);

    // Wait untuk result
    EXPECT_EQ(future.get(), 42);
}

// Test event ordering
TEST_F(EventTest, EventOrdering) {
    std::vector<int> sequence;
    std::mutex mutex;

    // Add listener
    emitter_->addListener("sequence", [&](const Event& e) {
        std::lock_guard<std::mutex> lock(mutex);
        sequence.push_back(e.get<int>());
    });

    // Emit events
    for (int i = 0; i < 10; ++i) {
        emitter_->emit("sequence", i);
    }

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify sequence
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(sequence.size(), 10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(sequence[i], i);
    }
}

// Test event loop control
TEST_F(EventTest, EventLoopControl) {
    EXPECT_TRUE(loop_->isRunning());

    loop_->stop();
    EXPECT_FALSE(loop_->isRunning());

    loop_->start();
    EXPECT_TRUE(loop_->isRunning());
}

// Test event data types
TEST_F(EventTest, EventDataTypes) {
    // Test dengan berbagai tipe data
    emitter_->addListener("int", [](const Event& e) {
        EXPECT_EQ(e.get<int>(), 42);
    });

    emitter_->addListener("string", [](const Event& e) {
        EXPECT_EQ(e.get<std::string>(), "test");
    });

    emitter_->addListener("vector", [](const Event& e) {
        const auto& vec = e.get<std::vector<int>>();
        EXPECT_EQ(vec.size(), 3);
        EXPECT_EQ(vec[0], 1);
        EXPECT_EQ(vec[1], 2);
        EXPECT_EQ(vec[2], 3);
    });

    // Emit events
    emitter_->emit("int", 42);
    emitter_->emit("string", std::string("test"));
    emitter_->emit("vector", std::vector<int>{1, 2, 3});

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test error handling
TEST_F(EventTest, ErrorHandling) {
    // Test invalid type cast
    emitter_->addListener("test", [](const Event& e) {
        EXPECT_THROW(e.get<int>(), Error);  // Data is string
    });

    emitter_->emit("test", std::string("not an int"));

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test listener management
TEST_F(EventTest, ListenerManagement) {
    std::atomic<int> counter{0};

    // Add listener
    emitter_->addListener("test", [&](const Event&) { counter++; });

    // Remove listener
    emitter_->removeListener("test");

    // Emit event
    emitter_->emit("test");

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 0);

    // Add listener again
    emitter_->addListener("test", [&](const Event&) { counter++; });

    // Remove all listeners
    emitter_->removeAllListeners();

    // Emit event
    emitter_->emit("test");

    // Wait untuk processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 0);
}

// Stress test
TEST_F(EventTest, StressTest) {
    static constexpr int kNumEvents = 10000;
    static constexpr int kNumListeners = 10;

    std::atomic<int> counter{0};

    // Add listeners
    for (int i = 0; i < kNumListeners; ++i) {
        emitter_->addListener("stress", [&](const Event&) { counter++; });
    }

    // Emit events dari multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kNumEvents; ++j) {
                emitter_->emit("stress");
            }
        });
    }

    // Wait untuk threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Wait untuk processing
    while (loop_->queueSize() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify total events processed
    EXPECT_EQ(counter, kNumEvents * 4 * kNumListeners);
}

} // namespace fmus::core::test