#include "fmus/sip/message.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <algorithm>

namespace fmus::sip {

// SipUri implementation
SipUri::SipUri(const std::string& uri) {
    // Simple URI parsing - just extract basic components
    // Format: sip:user@host:port;parameters
    
    size_t scheme_pos = uri.find(":");
    if (scheme_pos != std::string::npos) {
        scheme = uri.substr(0, scheme_pos);
        
        std::string rest = uri.substr(scheme_pos + 1);
        
        // Find parameters
        size_t param_pos = rest.find(";");
        std::string uri_part = (param_pos != std::string::npos) ? rest.substr(0, param_pos) : rest;
        
        // Parse user@host:port
        size_t at_pos = uri_part.find("@");
        if (at_pos != std::string::npos) {
            user = uri_part.substr(0, at_pos);
            std::string host_port = uri_part.substr(at_pos + 1);
            
            size_t port_pos = host_port.find(":");
            if (port_pos != std::string::npos) {
                host = host_port.substr(0, port_pos);
                port = std::stoi(host_port.substr(port_pos + 1));
            } else {
                host = host_port;
            }
        } else {
            // No user part
            size_t port_pos = uri_part.find(":");
            if (port_pos != std::string::npos) {
                host = uri_part.substr(0, port_pos);
                port = std::stoi(uri_part.substr(port_pos + 1));
            } else {
                host = uri_part;
            }
        }
    }
}

std::string SipUri::toString() const {
    std::ostringstream oss;
    oss << scheme << ":";
    if (!user.empty()) {
        oss << user << "@";
    }
    oss << host;
    if (port != 5060) {
        oss << ":" << port;
    }
    
    for (const auto& [key, value] : parameters) {
        oss << ";" << key << "=" << value;
    }
    
    return oss.str();
}

// SipHeaders implementation
void SipHeaders::set(const std::string& name, const std::string& value) {
    headers_[name] = value;
}

std::string SipHeaders::get(const std::string& name) const {
    auto it = headers_.find(name);
    return (it != headers_.end()) ? it->second : "";
}

bool SipHeaders::has(const std::string& name) const {
    return headers_.find(name) != headers_.end();
}

void SipHeaders::remove(const std::string& name) {
    headers_.erase(name);
}

size_t SipHeaders::getContentLength() const {
    std::string length_str = get("Content-Length");
    return length_str.empty() ? 0 : std::stoul(length_str);
}

// SipMessage implementation
SipMessage::SipMessage(SipMethod method, const SipUri& request_uri)
    : is_request_(true), method_(method), request_uri_(request_uri) {
}

SipMessage::SipMessage(SipResponseCode code, const std::string& reason_phrase)
    : is_request_(false), response_code_(code), reason_phrase_(reason_phrase) {
    if (reason_phrase_.empty()) {
        reason_phrase_ = responseCodeToString(code);
    }
}

std::string SipMessage::toString() const {
    std::ostringstream oss;
    
    if (is_request_) {
        oss << methodToString(method_) << " " << request_uri_.toString() << " SIP/2.0\r\n";
    } else {
        oss << "SIP/2.0 " << static_cast<int>(response_code_) << " " << reason_phrase_ << "\r\n";
    }
    
    // Add headers
    for (const auto& [name, value] : headers_.getAll()) {
        oss << name << ": " << value << "\r\n";
    }
    
    oss << "\r\n";
    
    if (!body_.empty()) {
        oss << body_;
    }
    
    return oss.str();
}

SipMessage SipMessage::fromString(const std::string& message) {
    // Simple parsing - split by lines
    std::istringstream iss(message);
    std::string line;
    
    // Parse first line
    if (!std::getline(iss, line)) {
        throw std::runtime_error("Invalid SIP message: empty");
    }
    
    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    
    SipMessage msg;
    
    // Check if request or response
    if (line.find("SIP/2.0") == 0) {
        // Response
        msg.is_request_ = false;
        std::istringstream line_stream(line);
        std::string version, code_str;
        line_stream >> version >> code_str;
        msg.response_code_ = static_cast<SipResponseCode>(std::stoi(code_str));
        
        // Get reason phrase (rest of line)
        std::string reason;
        std::getline(line_stream, reason);
        if (!reason.empty() && reason[0] == ' ') {
            reason = reason.substr(1);
        }
        msg.reason_phrase_ = reason;
    } else {
        // Request
        msg.is_request_ = true;
        std::istringstream line_stream(line);
        std::string method_str, uri_str, version;
        line_stream >> method_str >> uri_str >> version;
        msg.method_ = stringToMethod(method_str);
        msg.request_uri_ = SipUri(uri_str);
    }
    
    // Parse headers
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            // End of headers
            break;
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            msg.headers_.set(name, value);
        }
    }
    
    // Parse body (rest of message)
    std::ostringstream body_stream;
    while (std::getline(iss, line)) {
        body_stream << line << "\n";
    }
    std::string body = body_stream.str();
    if (!body.empty() && body.back() == '\n') {
        body.pop_back();
    }
    msg.body_ = body;
    
    return msg;
}

// Utility functions
std::string methodToString(SipMethod method) {
    switch (method) {
        case SipMethod::INVITE: return "INVITE";
        case SipMethod::ACK: return "ACK";
        case SipMethod::BYE: return "BYE";
        case SipMethod::CANCEL: return "CANCEL";
        case SipMethod::REGISTER: return "REGISTER";
        case SipMethod::OPTIONS: return "OPTIONS";
        case SipMethod::INFO: return "INFO";
        case SipMethod::PRACK: return "PRACK";
        case SipMethod::UPDATE: return "UPDATE";
        case SipMethod::REFER: return "REFER";
        case SipMethod::NOTIFY: return "NOTIFY";
        case SipMethod::SUBSCRIBE: return "SUBSCRIBE";
        case SipMethod::MESSAGE: return "MESSAGE";
        default: return "UNKNOWN";
    }
}

SipMethod stringToMethod(const std::string& method) {
    if (method == "INVITE") return SipMethod::INVITE;
    if (method == "ACK") return SipMethod::ACK;
    if (method == "BYE") return SipMethod::BYE;
    if (method == "CANCEL") return SipMethod::CANCEL;
    if (method == "REGISTER") return SipMethod::REGISTER;
    if (method == "OPTIONS") return SipMethod::OPTIONS;
    if (method == "INFO") return SipMethod::INFO;
    if (method == "PRACK") return SipMethod::PRACK;
    if (method == "UPDATE") return SipMethod::UPDATE;
    if (method == "REFER") return SipMethod::REFER;
    if (method == "NOTIFY") return SipMethod::NOTIFY;
    if (method == "SUBSCRIBE") return SipMethod::SUBSCRIBE;
    if (method == "MESSAGE") return SipMethod::MESSAGE;
    return SipMethod::INVITE; // Default
}

std::string responseCodeToString(SipResponseCode code) {
    switch (code) {
        case SipResponseCode::Trying: return "Trying";
        case SipResponseCode::Ringing: return "Ringing";
        case SipResponseCode::SessionProgress: return "Session Progress";
        case SipResponseCode::OK: return "OK";
        case SipResponseCode::Accepted: return "Accepted";
        case SipResponseCode::MultipleChoices: return "Multiple Choices";
        case SipResponseCode::MovedPermanently: return "Moved Permanently";
        case SipResponseCode::MovedTemporarily: return "Moved Temporarily";
        case SipResponseCode::BadRequest: return "Bad Request";
        case SipResponseCode::Unauthorized: return "Unauthorized";
        case SipResponseCode::Forbidden: return "Forbidden";
        case SipResponseCode::NotFound: return "Not Found";
        case SipResponseCode::MethodNotAllowed: return "Method Not Allowed";
        case SipResponseCode::RequestTimeout: return "Request Timeout";
        case SipResponseCode::ServerInternalError: return "Internal Server Error";
        case SipResponseCode::NotImplemented: return "Not Implemented";
        case SipResponseCode::BadGateway: return "Bad Gateway";
        case SipResponseCode::ServiceUnavailable: return "Service Unavailable";
        case SipResponseCode::BusyEverywhere: return "Busy Everywhere";
        case SipResponseCode::Decline: return "Decline";
        default: return "Unknown";
    }
}

} // namespace fmus::sip
