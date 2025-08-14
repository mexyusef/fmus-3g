#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace fmus::core {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class Logger {
public:
    static void setLevel(LogLevel level);
    static LogLevel getLevel();
    
    template<typename... Args>
    static void debug(const std::string& format, Args&&... args) {
        log(LogLevel::DEBUG, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(const std::string& format, Args&&... args) {
        log(LogLevel::INFO, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warn(const std::string& format, Args&&... args) {
        log(LogLevel::WARN, format, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(const std::string& format, Args&&... args) {
        log(LogLevel::ERROR, format, std::forward<Args>(args)...);
    }

private:
    static LogLevel current_level_;
    static std::mutex mutex_;
    
    template<typename... Args>
    static void log(LogLevel level, const std::string& format, Args&&... args) {
        if (level < current_level_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        // Format timestamp
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        
        // Format level
        const char* level_str = "";
        switch (level) {
            case LogLevel::DEBUG: level_str = "DEBUG"; break;
            case LogLevel::INFO:  level_str = "INFO "; break;
            case LogLevel::WARN:  level_str = "WARN "; break;
            case LogLevel::ERROR: level_str = "ERROR"; break;
        }
        
        // Simple format string replacement (basic implementation)
        std::string message = formatString(format, std::forward<Args>(args)...);
        
        std::cout << "[" << oss.str() << "] [" << level_str << "] " << message << std::endl;
    }
    
    // Simple format string implementation
    template<typename T>
    static std::string formatString(const std::string& format, T&& value) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            std::ostringstream oss;
            oss << value;
            std::string result = format;
            result.replace(pos, 2, oss.str());
            return result;
        }
        return format;
    }
    
    template<typename T, typename... Args>
    static std::string formatString(const std::string& format, T&& value, Args&&... args) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            std::ostringstream oss;
            oss << value;
            std::string partial = format;
            partial.replace(pos, 2, oss.str());
            return formatString(partial, std::forward<Args>(args)...);
        }
        return format;
    }
    
    static std::string formatString(const std::string& format) {
        return format;
    }
};

} // namespace fmus::core
