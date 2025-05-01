#pragma once

#include <string>
#include <string_view>
#include <system_error>
#include <exception>
#include <memory>
#include <optional>

// Untuk C++20 features
#if __has_include(<source_location>) && __cplusplus >= 202002L
#include <source_location>
#else
#include <experimental/source_location>
namespace std {
    using source_location = experimental::source_location;
}
#endif

#include <fmus/core/logger.hpp>

namespace fmus::core {

// Kode error untuk fmus-3g
enum class ErrorCode {
    // System errors
    Success = 0,
    Unknown,
    InvalidArgument,
    InvalidState,
    NotImplemented,
    NotSupported,

    // Network errors
    NetworkError,
    ConnectionFailed,
    ConnectionClosed,
    ConnectionTimeout,
    InvalidAddress,

    // Media errors
    MediaError,
    CodecNotFound,
    DeviceNotFound,
    InvalidFormat,

    // IVR errors
    IvrError,
    ScriptError,
    ScriptTimeout,

    // API errors
    ApiError,
    AuthenticationFailed,
    InvalidRequest,
    ResourceNotFound,

    // Resource errors
    OutOfMemory,
    FileNotFound,
    FileAccessDenied,
    InvalidData
};

// Error category untuk fmus-3g
class ErrorCategory : public std::error_category {
public:
    static const ErrorCategory& instance() {
        static ErrorCategory instance;
        return instance;
    }

    const char* name() const noexcept override { return "fmus"; }

    std::string message(int ev) const override;

    // Untuk error condition mapping
    std::error_condition default_error_condition(int ev) const noexcept override;

private:
    ErrorCategory() = default;
};

// Helper untuk membuat error code
inline std::error_code make_error_code(ErrorCode e) {
    return {static_cast<int>(e), ErrorCategory::instance()};
}

// Base exception class untuk fmus-3g
class Error : public std::runtime_error {
public:
    Error(ErrorCode code,
          std::string_view message,
          const std::source_location& location = std::source_location::current())
        : std::runtime_error(std::string(message))
        , code_(code)
        , location_(location) {
        // Log error saat exception dibuat
        logger().error("[{}] {} at {}:{}",
            ErrorCategory::instance().message(static_cast<int>(code)),
            message,
            location.file_name(),
            location.line());
    }

    ErrorCode code() const noexcept { return code_; }
    const std::source_location& location() const noexcept { return location_; }
    std::error_code error_code() const noexcept { return make_error_code(code_); }

private:
    ErrorCode code_;
    std::source_location location_;
};

// Helper untuk throw error dengan source location
template<typename... Args>
[[noreturn]] void throw_error(ErrorCode code,
                             std::string_view message,
                             const std::source_location& location = std::source_location::current()) {
    throw Error(code, message, location);
}

// Result type untuk error handling tanpa exceptions
template<typename T>
class Result {
public:
    // Constructors untuk value
    Result(T value) : value_(std::move(value)) {}

    // Constructors untuk error
    Result(ErrorCode code, std::string_view message)
        : error_(Error(code, message)) {}

    // Check status
    bool is_ok() const noexcept { return !error_.has_value(); }
    bool is_error() const noexcept { return error_.has_value(); }

    // Access value/error
    const T& value() const& {
        if (error_) throw *error_;
        return *value_;
    }

    T&& value() && {
        if (error_) throw *error_;
        return std::move(*value_);
    }

    const Error& error() const {
        if (!error_) throw_error(ErrorCode::InvalidState, "Result contains no error");
        return *error_;
    }

    // Conversion operators
    explicit operator bool() const noexcept { return is_ok(); }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

// Specialization untuk void
template<>
class Result<void> {
public:
    Result() = default;
    Result(ErrorCode code, std::string_view message)
        : error_(Error(code, message)) {}

    bool is_ok() const noexcept { return !error_.has_value(); }
    bool is_error() const noexcept { return error_.has_value(); }

    void value() const {
        if (error_) throw *error_;
    }

    const Error& error() const {
        if (!error_) throw_error(ErrorCode::InvalidState, "Result contains no error");
        return *error_;
    }

    explicit operator bool() const noexcept { return is_ok(); }

private:
    std::optional<Error> error_;
};

} // namespace fmus::core

// Enable ErrorCode to be used with std::error_code
namespace std {
    template<>
    struct is_error_code_enum<fmus::core::ErrorCode> : true_type {};
}