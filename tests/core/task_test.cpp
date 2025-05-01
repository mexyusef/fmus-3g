#include <gtest/gtest.h>
#include <fmus/core/task.hpp>
#include <thread>
#include <atomic>

namespace fmus::core::test {

class TaskTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = make_task_scheduler();
        scheduler_->start();
    }

    void TearDown() override {
        scheduler_->stop();
        scheduler_.reset();
    }

    std::unique_ptr<TaskScheduler> scheduler_;
};

// Helper untuk membuat async task
Task<int> async_add(int a, int b) {
    co_return a + b;
}

Task<void> async_increment(std::atomic<int>& counter) {
    counter++;
    co_return;
}

Task<std::string> async_concat(std::string a, std::string b) {
    co_await sleep_for(std::chrono::milliseconds(10));
    co_return a + b;
}

// Test basic task execution
TEST_F(TaskTest, BasicExecution) {
    auto task = async_add(2, 3);
    scheduler_->runSync(task);

    ASSERT_TRUE(task.isCompleted());
    EXPECT_EQ(task.result().value(), 5);
}

// Test multiple tasks
TEST_F(TaskTest, MultipleTasks) {
    std::atomic<int> counter{0};
    std::vector<Task<void>> tasks;

    // Create tasks
    for (int i = 0; i < 10; ++i) {
        tasks.push_back(async_increment(counter));
    }

    // Schedule tasks
    for (auto& task : tasks) {
        scheduler_->schedule(task);
    }

    // Wait untuk completion
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 10);
}

// Test task priorities
TEST_F(TaskTest, TaskPriorities) {
    std::vector<int> sequence;
    std::mutex mutex;

    auto make_task = [&](int value, TaskPriority priority) -> Task<void> {
        {
            std::lock_guard<std::mutex> lock(mutex);
            sequence.push_back(value);
        }
        co_return;
    };

    // Schedule tasks dengan different priorities
    scheduler_->schedule(make_task(1, TaskPriority::Low));
    scheduler_->schedule(make_task(2, TaskPriority::High));
    scheduler_->schedule(make_task(3, TaskPriority::Critical));
    scheduler_->schedule(make_task(4, TaskPriority::Normal));

    // Wait untuk completion
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify execution order (higher priority first)
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(sequence.size(), 4);
    EXPECT_EQ(sequence[0], 3);  // Critical
    EXPECT_EQ(sequence[1], 2);  // High
    EXPECT_EQ(sequence[2], 4);  // Normal
    EXPECT_EQ(sequence[3], 1);  // Low
}

// Test async operations
TEST_F(TaskTest, AsyncOperations) {
    auto task = async_concat("Hello, ", "World!");
    scheduler_->schedule(task);

    // Wait untuk completion
    while (!task.isCompleted()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(task.result().value(), "Hello, World!");
}

// Test error handling
TEST_F(TaskTest, ErrorHandling) {
    auto error_task = []() -> Task<int> {
        throw std::runtime_error("Test error");
        co_return 0;
    }();

    scheduler_->runSync(error_task);

    ASSERT_TRUE(error_task.isCompleted());
    ASSERT_TRUE(error_task.result().hasError());
    EXPECT_THROW(error_task.result().value(), std::runtime_error);
}

// Test task cancellation
TEST_F(TaskTest, TaskCancellation) {
    bool task_ran = false;

    auto long_task = [&]() -> Task<void> {
        co_await sleep_for(std::chrono::milliseconds(1000));
        task_ran = true;
        co_return;
    }();

    scheduler_->schedule(long_task);
    scheduler_->stop();  // Stop before task completes

    EXPECT_FALSE(task_ran);
}

// Test scheduler control
TEST_F(TaskTest, SchedulerControl) {
    EXPECT_TRUE(scheduler_->isRunning());

    scheduler_->stop();
    EXPECT_FALSE(scheduler_->isRunning());

    scheduler_->start();
    EXPECT_TRUE(scheduler_->isRunning());
}

// Test concurrent task execution
TEST_F(TaskTest, ConcurrentExecution) {
    static constexpr int kNumTasks = 1000;
    std::atomic<int> completed{0};

    auto increment_task = [&]() -> Task<void> {
        co_await sleep_for(std::chrono::milliseconds(1));
        completed++;
        co_return;
    };

    // Schedule many tasks
    std::vector<Task<void>> tasks;
    for (int i = 0; i < kNumTasks; ++i) {
        tasks.push_back(increment_task());
        scheduler_->schedule(tasks.back());
    }

    // Wait untuk completion
    while (completed < kNumTasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(completed, kNumTasks);
}

// Test nested tasks
TEST_F(TaskTest, NestedTasks) {
    std::atomic<int> counter{0};

    auto nested_task = [&]() -> Task<void> {
        co_await async_increment(counter);
        co_await async_increment(counter);
        co_return;
    };

    auto parent_task = [&]() -> Task<void> {
        co_await nested_task();
        co_await async_increment(counter);
        co_return;
    };

    scheduler_->runSync(parent_task());
    EXPECT_EQ(counter, 3);
}

// Test task chaining
TEST_F(TaskTest, TaskChaining) {
    auto task1 = async_add(2, 3);

    auto task2 = [](Task<int>& t1) -> Task<int> {
        auto result = co_await t1;
        co_return result * 2;
    }(task1);

    scheduler_->runSync(task2);
    EXPECT_EQ(task2.result().value(), 10);
}

} // namespace fmus::core::test