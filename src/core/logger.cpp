#include "fmus/core/logger.hpp"

namespace fmus::core {

LogLevel Logger::current_level_ = LogLevel::INFO;
std::mutex Logger::mutex_;

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_level_ = level;
}

LogLevel Logger::getLevel() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_level_;
}

} // namespace fmus::core
