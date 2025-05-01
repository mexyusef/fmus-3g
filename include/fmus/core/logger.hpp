#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <chrono>
#include <mutex>
#include <fstream>

// Untuk C++20 features
#if __has_include(<source_location>) && __cplusplus >= 202002L
#include <source_location>
#else
#include <experimental/source_location>
namespace std {
    using source_location = experimental::source_location;
}
#endif

#if __has_include(<format>) && __cplusplus >= 202002L
#include <format>
#else
#include <sstream>
#include <iomanip>
#endif

namespace fmus::core {

// Tingkat log yang tersedia
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

// Format pesan log
struct LogMessage {
    LogLevel level;
    std::string message;  // Changed from string_view to string for lifetime management
    std::source_location location;
    std::chrono::system_clock::time_point timestamp;
};

// Interface untuk sink log
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogMessage& msg) = 0;
};

// Sink untuk console output
class ConsoleSink : public ILogSink {
public:
    void write(const LogMessage& msg) override;
};

// Sink untuk file output
class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& filename);
    void write(const LogMessage& msg) override;

private:
    std::ofstream file_;
    std::mutex mutex_;
};

// Helper untuk format string jika std::format tidak tersedia
#if !(__has_include(<format>) && __cplusplus >= 202002L)
namespace detail {
    template<typename... Args>
    std::string format_string(const std::string_view& fmt, Args&&... args) {
        std::ostringstream oss;
        size_t pos = 0;
        size_t arg_idx = 0;
        ((void)[&] {
            size_t next = fmt.find("{}", pos);
            if (next != std::string::npos) {
                oss << fmt.substr(pos, next - pos);
                oss << args;
                pos = next + 2;
            }
        }(), ...);
        oss << fmt.substr(pos);
        return oss.str();
    }
}
#endif

// Logger utama
class Logger {
public:
    static Logger& instance();

    // Mencegah copy dan move
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // Menambah sink baru
    void addSink(std::shared_ptr<ILogSink> sink);

    // Set minimum log level
    void setLevel(LogLevel level);

    // Fungsi logging utama
    template<typename... Args>
    void log(LogLevel level,
             std::string_view format,
             Args&&... args,
             const std::source_location& location = std::source_location::current()) {
        if (level < minimumLevel_) return;

        LogMessage msg{
            .level = level,
#if __has_include(<format>) && __cplusplus >= 202002L
            .message = std::format(format, std::forward<Args>(args)...),
#else
            .message = detail::format_string(format, std::forward<Args>(args)...),
#endif
            .location = location,
            .timestamp = std::chrono::system_clock::now()
        };

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& sink : sinks_) {
            sink->write(msg);
        }
    }

    // Helper functions
    template<typename... Args>
    void trace(std::string_view format, Args&&... args) {
        log(LogLevel::Trace, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void debug(std::string_view format, Args&&... args) {
        log(LogLevel::Debug, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(std::string_view format, Args&&... args) {
        log(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(std::string_view format, Args&&... args) {
        log(LogLevel::Warning, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(std::string_view format, Args&&... args) {
        log(LogLevel::Error, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void fatal(std::string_view format, Args&&... args) {
        log(LogLevel::Fatal, format, std::forward<Args>(args)...);
    }

private:
    Logger() = default;

    std::vector<std::shared_ptr<ILogSink>> sinks_;
    LogLevel minimumLevel_ = LogLevel::Info;
    std::mutex mutex_;
};

// Global logger instance
inline Logger& logger() {
    return Logger::instance();
}

} // namespace fmus::core