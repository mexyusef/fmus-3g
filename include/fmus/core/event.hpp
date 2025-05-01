#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <any>
#include <chrono>
#include <future>
#include <queue>
#include <condition_variable>

#include <fmus/core/error.hpp>

namespace fmus::core {

// Forward declarations
class EventLoop;
class Event;
class EventEmitter;

using EventCallback = std::function<void(const Event&)>;
using EventFilter = std::function<bool(const Event&)>;
using EventId = std::uint64_t;

// Event class untuk menyimpan data event
class Event {
public:
    // Constructor
    Event(std::string type, std::any data = std::any())
        : type_(std::move(type))
        , data_(std::move(data))
        , timestamp_(std::chrono::system_clock::now()) {}

    // Getters
    const std::string& type() const noexcept { return type_; }
    const std::any& data() const noexcept { return data_; }
    auto timestamp() const noexcept { return timestamp_; }
    EventId id() const noexcept { return id_; }

    // Data access dengan type checking
    template<typename T>
    const T& get() const {
        try {
            return std::any_cast<const T&>(data_);
        } catch (const std::bad_any_cast&) {
            throw_error(ErrorCode::InvalidArgument,
                "Invalid event data type cast");
        }
    }

private:
    friend class EventEmitter;

    std::string type_;
    std::any data_;
    std::chrono::system_clock::time_point timestamp_;
    EventId id_ = 0;
};

// Event listener untuk menerima events
class EventListener {
public:
    explicit EventListener(EventCallback callback, EventFilter filter = nullptr)
        : callback_(std::move(callback))
        , filter_(std::move(filter)) {}

    // Handle event
    void handle(const Event& event) const {
        if (!filter_ || filter_(event)) {
            callback_(event);
        }
    }

private:
    EventCallback callback_;
    EventFilter filter_;
};

// Event emitter untuk mengirim events
class EventEmitter {
public:
    explicit EventEmitter(EventLoop& loop);
    ~EventEmitter();

    // Non-copyable
    EventEmitter(const EventEmitter&) = delete;
    EventEmitter& operator=(const EventEmitter&) = delete;

    // Movable
    EventEmitter(EventEmitter&&) noexcept = default;
    EventEmitter& operator=(EventEmitter&&) noexcept = default;

    // Emit event
    void emit(Event event);
    void emit(std::string type, std::any data = std::any());

    // Add/remove listeners
    void addListener(const std::string& type, EventCallback callback);
    void addListener(EventCallback callback, EventFilter filter = nullptr);
    void removeListener(const std::string& type);
    void removeAllListeners();

    // Async event handling
    template<typename T>
    std::future<T> emitAsync(Event event) {
        auto promise = std::make_shared<std::promise<T>>();
        auto future = promise->get_future();

        addListener(event.type(), [promise](const Event& e) {
            try {
                promise->set_value(e.get<T>());
            } catch (const std::exception& ex) {
                promise->set_exception(std::current_exception());
            }
        });

        emit(std::move(event));
        return future;
    }

private:
    EventLoop& loop_;
    std::unordered_map<std::string, std::vector<EventListener>> listeners_;
    std::vector<EventListener> global_listeners_;
    std::mutex mutex_;
    EventId next_id_ = 1;
};

// Event loop untuk processing events
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Non-copyable
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Start/stop loop
    void start();
    void stop();

    // Post event ke queue
    void post(Event event);

    // Process single event
    bool processOne();

    // Process all pending events
    void processAll();

    // Check if running
    bool isRunning() const noexcept { return running_; }

    // Get queue size
    std::size_t queueSize() const;

private:
    friend class EventEmitter;

    // Worker thread function
    void run();

    std::queue<Event> event_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool running_ = false;
    bool stop_requested_ = false;
};

// Helper untuk membuat event emitter
inline std::unique_ptr<EventEmitter> make_event_emitter(EventLoop& loop) {
    return std::make_unique<EventEmitter>(loop);
}

// Helper untuk membuat event loop
inline std::unique_ptr<EventLoop> make_event_loop() {
    return std::make_unique<EventLoop>();
}

} // namespace fmus::core