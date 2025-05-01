#include <fmus/core/event.hpp>
#include <fmus/core/logger.hpp>

namespace fmus::core {

// EventEmitter implementation

EventEmitter::EventEmitter(EventLoop& loop)
    : loop_(loop) {}

EventEmitter::~EventEmitter() {
    removeAllListeners();
}

void EventEmitter::emit(Event event) {
    // Set event ID
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.id_ = next_id_++;
    }

    // Post ke event loop
    loop_.post(std::move(event));
}

void EventEmitter::emit(std::string type, std::any data) {
    emit(Event(std::move(type), std::move(data)));
}

void EventEmitter::addListener(const std::string& type, EventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_[type].emplace_back(std::move(callback));
}

void EventEmitter::addListener(EventCallback callback, EventFilter filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    global_listeners_.emplace_back(std::move(callback), std::move(filter));
}

void EventEmitter::removeListener(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(type);
}

void EventEmitter::removeAllListeners() {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.clear();
    global_listeners_.clear();
}

// EventLoop implementation

EventLoop::EventLoop() = default;

EventLoop::~EventLoop() {
    if (running_) {
        stop();
    }
}

void EventLoop::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;

    running_ = true;
    stop_requested_ = false;
    worker_ = std::thread(&EventLoop::run, this);
}

void EventLoop::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        stop_requested_ = true;
    }

    cv_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }

    running_ = false;
}

void EventLoop::post(Event event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event_queue_.push(std::move(event));
    }
    cv_.notify_one();
}

bool EventLoop::processOne() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (event_queue_.empty()) {
        return false;
    }

    // Get event dari queue
    Event event = std::move(event_queue_.front());
    event_queue_.pop();

    // Process event di luar lock
    lock.unlock();

    try {
        // Dispatch ke semua listeners
        for (const auto& [type, listeners] : listeners_) {
            if (type == event.type()) {
                for (const auto& listener : listeners) {
                    listener.handle(event);
                }
            }
        }

        // Dispatch ke global listeners
        for (const auto& listener : global_listeners_) {
            listener.handle(event);
        }
    }
    catch (const std::exception& e) {
        logger().error("Error processing event: {}", e.what());
    }

    return true;
}

void EventLoop::processAll() {
    while (processOne()) {}
}

std::size_t EventLoop::queueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return event_queue_.size();
}

void EventLoop::run() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!stop_requested_) {
        // Wait untuk event atau stop request
        cv_.wait(lock, [this] {
            return !event_queue_.empty() || stop_requested_;
        });

        if (stop_requested_) break;

        // Process event
        Event event = std::move(event_queue_.front());
        event_queue_.pop();

        // Release lock selama processing
        lock.unlock();

        try {
            // Dispatch ke semua listeners
            for (const auto& [type, listeners] : listeners_) {
                if (type == event.type()) {
                    for (const auto& listener : listeners) {
                        listener.handle(event);
                    }
                }
            }

            // Dispatch ke global listeners
            for (const auto& listener : global_listeners_) {
                listener.handle(event);
            }
        }
        catch (const std::exception& e) {
            logger().error("Error processing event: {}", e.what());
        }

        lock.lock();
    }
}

} // namespace fmus::core