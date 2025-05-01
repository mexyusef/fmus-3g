#pragma once

#include <coroutine>
#include <memory>
#include <future>
#include <queue>
#include <thread>
#include <functional>
#include <variant>
#include <optional>
#include <chrono>
#include <condition_variable>

#include <fmus/core/error.hpp>
#include <fmus/core/event.hpp>

namespace fmus::core {

// Forward declarations
template<typename T> class Task;
class TaskScheduler;
class TaskPromise;

// Task status untuk tracking state
enum class TaskStatus {
    Created,
    Running,
    Suspended,
    Completed,
    Failed
};

// Task priority untuk scheduling
enum class TaskPriority {
    Low,
    Normal,
    High,
    Critical
};

// Task result wrapper
template<typename T>
class TaskResult {
public:
    // Constructors
    TaskResult() = default;
    explicit TaskResult(T value) : value_(std::move(value)) {}
    explicit TaskResult(std::exception_ptr error) : error_(error) {}

    // Check status
    bool hasValue() const noexcept { return value_.has_value(); }
    bool hasError() const noexcept { return error_ != nullptr; }

    // Get value/error
    const T& value() const {
        if (!hasValue()) {
            if (hasError()) {
                std::rethrow_exception(error_);
            }
            throw_error(ErrorCode::InvalidState, "Task has no value");
        }
        return *value_;
    }

    std::exception_ptr error() const noexcept { return error_; }

private:
    std::optional<T> value_;
    std::exception_ptr error_ = nullptr;
};

// Task promise type
template<typename T>
class TaskPromise {
public:
    Task<T> get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }

    template<typename U>
    void return_value(U&& value) {
        result_ = TaskResult<T>(std::forward<U>(value));
    }

    void unhandled_exception() {
        result_ = TaskResult<T>(std::current_exception());
    }

    TaskResult<T>& result() noexcept { return result_; }

private:
    TaskResult<T> result_;
};

// Task specialization untuk void
template<>
class TaskPromise<void> {
public:
    Task<void> get_return_object() noexcept;
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }

    void return_void() {}

    void unhandled_exception() {
        error_ = std::current_exception();
    }

    std::exception_ptr error() const noexcept { return error_; }

private:
    std::exception_ptr error_ = nullptr;
};

// Task class untuk async operations
template<typename T = void>
class Task {
public:
    using promise_type = TaskPromise<T>;

    // Constructor dari handle
    explicit Task(std::coroutine_handle<promise_type> handle)
        : handle_(handle) {}

    // Destructor
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // Non-copyable
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    // Movable
    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    // Get result
    TaskResult<T>& result() { return handle_.promise().result(); }

    // Check if completed
    bool isCompleted() const noexcept {
        return !handle_ || handle_.done();
    }

    // Resume execution
    void resume() {
        if (handle_ && !handle_.done()) {
            handle_();
        }
    }

    // Get handle
    auto handle() const noexcept { return handle_; }

private:
    std::coroutine_handle<promise_type> handle_;
};

// Task scheduler untuk managing async tasks
class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler();

    // Non-copyable
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    // Schedule task
    template<typename T>
    void schedule(Task<T>& task, TaskPriority priority = TaskPriority::Normal) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push({task.handle(), priority});
        cv_.notify_one();
    }

    // Run task immediately
    template<typename T>
    void runSync(Task<T>& task) {
        while (!task.isCompleted()) {
            task.resume();
        }
    }

    // Start scheduler
    void start(std::size_t thread_count = std::thread::hardware_concurrency());

    // Stop scheduler
    void stop();

    // Check if running
    bool isRunning() const noexcept { return running_; }

    // Get queue size
    std::size_t queueSize() const;

private:
    // Task entry untuk queue
    struct TaskEntry {
        std::coroutine_handle<> handle;
        TaskPriority priority;

        // Compare untuk priority queue
        bool operator<(const TaskEntry& other) const {
            return priority < other.priority;
        }
    };

    // Worker thread function
    void run();

    std::priority_queue<TaskEntry> tasks_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
    bool stop_requested_ = false;
};

// Helper untuk membuat task scheduler
inline std::unique_ptr<TaskScheduler> make_task_scheduler() {
    return std::make_unique<TaskScheduler>();
}

// Awaitable untuk sleeping
class SleepAwaitable {
public:
    explicit SleepAwaitable(std::chrono::milliseconds duration)
        : duration_(duration) {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        std::thread([handle, this]() {
            std::this_thread::sleep_for(duration_);
            handle.resume();
        }).detach();
    }
    void await_resume() noexcept {}

private:
    std::chrono::milliseconds duration_;
};

// Helper untuk sleeping
inline SleepAwaitable sleep_for(std::chrono::milliseconds duration) {
    return SleepAwaitable(duration);
}

} // namespace fmus::core