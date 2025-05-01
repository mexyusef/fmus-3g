#include <fmus/core/error.hpp>
#include <unordered_map>

namespace fmus::core {

namespace {
    // Map untuk error messages
    const std::unordered_map<ErrorCode, const char*> ERROR_MESSAGES = {
        // System errors
        {ErrorCode::Success, "Success"},
        {ErrorCode::Unknown, "Unknown error"},
        {ErrorCode::InvalidArgument, "Invalid argument"},
        {ErrorCode::InvalidState, "Invalid state"},
        {ErrorCode::NotImplemented, "Not implemented"},
        {ErrorCode::NotSupported, "Not supported"},

        // Network errors
        {ErrorCode::NetworkError, "Network error"},
        {ErrorCode::ConnectionFailed, "Connection failed"},
        {ErrorCode::ConnectionClosed, "Connection closed"},
        {ErrorCode::ConnectionTimeout, "Connection timeout"},
        {ErrorCode::InvalidAddress, "Invalid address"},

        // Media errors
        {ErrorCode::MediaError, "Media error"},
        {ErrorCode::CodecNotFound, "Codec not found"},
        {ErrorCode::DeviceNotFound, "Device not found"},
        {ErrorCode::InvalidFormat, "Invalid format"},

        // IVR errors
        {ErrorCode::IvrError, "IVR error"},
        {ErrorCode::ScriptError, "Script error"},
        {ErrorCode::ScriptTimeout, "Script timeout"},

        // API errors
        {ErrorCode::ApiError, "API error"},
        {ErrorCode::AuthenticationFailed, "Authentication failed"},
        {ErrorCode::InvalidRequest, "Invalid request"},
        {ErrorCode::ResourceNotFound, "Resource not found"},

        // Resource errors
        {ErrorCode::OutOfMemory, "Out of memory"},
        {ErrorCode::FileNotFound, "File not found"},
        {ErrorCode::FileAccessDenied, "File access denied"},
        {ErrorCode::InvalidData, "Invalid data"}
    };

    // Map untuk error conditions
    const std::unordered_map<ErrorCode, std::error_condition> ERROR_CONDITIONS = {
        {ErrorCode::NetworkError, std::errc::network_unreachable},
        {ErrorCode::ConnectionFailed, std::errc::connection_refused},
        {ErrorCode::ConnectionClosed, std::errc::connection_reset},
        {ErrorCode::ConnectionTimeout, std::errc::timed_out},
        {ErrorCode::OutOfMemory, std::errc::not_enough_memory},
        {ErrorCode::FileNotFound, std::errc::no_such_file_or_directory},
        {ErrorCode::FileAccessDenied, std::errc::permission_denied},
        {ErrorCode::InvalidData, std::errc::invalid_argument}
    };
}

std::string ErrorCategory::message(int ev) const {
    auto code = static_cast<ErrorCode>(ev);
    auto it = ERROR_MESSAGES.find(code);
    return it != ERROR_MESSAGES.end() ? it->second : "Unknown error";
}

std::error_condition ErrorCategory::default_error_condition(int ev) const noexcept {
    auto code = static_cast<ErrorCode>(ev);
    auto it = ERROR_CONDITIONS.find(code);
    return it != ERROR_CONDITIONS.end() ? it->second : std::error_condition(ev, *this);
}

} // namespace fmus::core