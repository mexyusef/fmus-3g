#include <fmus/core/logger.hpp>
#include <iostream>
#include <iomanip>
#include <array>
#include <chrono>
#include <sstream>

namespace fmus::core {

// Array untuk string representasi log level
constexpr std::array<const char*, 6> LEVEL_STRINGS = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

// Konversi LogLevel ke string
constexpr const char* levelToString(LogLevel level) {
    return LEVEL_STRINGS[static_cast<size_t>(level)];
}

// Implementasi singleton Logger
Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::setLevel(LogLevel level) {
    minimumLevel_ = level;
}

// Helper untuk format waktu
std::string formatTimestamp(const std::chrono::system_clock::time_point& timestamp) {
    auto time = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()
    ).count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms;
    return oss.str();
}

// Implementasi ConsoleSink
void ConsoleSink::write(const LogMessage& msg) {
    std::ostringstream oss;
    oss << formatTimestamp(msg.timestamp)
        << " [" << levelToString(msg.level) << "] "
        << msg.message
        << " (" << msg.location.file_name() << ":" << msg.location.line() << ")\n";

    std::cout << oss.str();
}

// Implementasi FileSink
FileSink::FileSink(const std::string& filename) {
    file_.open(filename, std::ios::app);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + filename);
    }
}

void FileSink::write(const LogMessage& msg) {
    std::ostringstream oss;
    oss << formatTimestamp(msg.timestamp)
        << " [" << levelToString(msg.level) << "] "
        << msg.message
        << " (" << msg.location.file_name() << ":" << msg.location.line() << ")\n";

    std::lock_guard<std::mutex> lock(mutex_);
    file_ << oss.str();
    file_.flush();
}

} // namespace fmus::core