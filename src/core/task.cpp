#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>

namespace fmus::core {

// TaskPromise implementation

template<typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
    return Task<T>(std::coroutine_handle<TaskPromise<T>>::from_promise(*this));
}

Task<void> TaskPromise<void>::get_return_object() noexcept {
    return Task<void>(std::coroutine_handle<TaskPromise<void>>::from_promise(*this));
}

// TaskScheduler implementation

TaskScheduler::TaskScheduler() = default;

TaskScheduler::~TaskScheduler() {
    if (running_) {
        stop();
    }
}

void TaskScheduler::start(std::size_t thread_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;

    running_ = true;
    stop_requested_ = false;

    // Start worker threads
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&TaskScheduler::run, this);
    }

    logger().info("Task scheduler started with {} worker threads", thread_count);
}

void TaskScheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;

        stop_requested_ = true;
    }

    // Notify all workers
    cv_.notify_all();

    // Wait untuk semua workers
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
    running_ = false;

    logger().info("Task scheduler stopped");
}

std::size_t TaskScheduler::queueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void TaskScheduler::run() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stop_requested_) {
        // Wait untuk task atau stop request
        cv_.wait(lock, [this] {
            return !tasks_.empty() || stop_requested_;
        });

        if (stop_requested_) break;

        // Get task dengan priority tertinggi
        auto entry = tasks_.top();
        tasks_.pop();

        // Release lock selama execution
        lock.unlock();

        try {
            // Resume task
            if (!entry.handle.done()) {
                entry.handle.resume();
            }

            // Reschedule jika belum selesai
            if (!entry.handle.done()) {
                std::lock_guard<std::mutex> schedule_lock(mutex_);
                tasks_.push(entry);
            }
        }
        catch (const std::exception& e) {
            logger().error("Error executing task: {}", e.what());
        }

        lock.lock();
    }
}

// Template instantiations untuk common types
template class TaskPromise<void>;
template class TaskPromise<int>;
template class TaskPromise<std::string>;
template class TaskPromise<bool>;

template class Task<void>;
template class Task<int>;
template class Task<std::string>;
template class Task<bool>;

} // namespace fmus::core