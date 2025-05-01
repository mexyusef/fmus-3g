#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>

#include <sstream>
#include <regex>
#include <random>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <unordered_map>

namespace fmus::sip {

// SIP headers implementation
SipHeaders::SipHeaders() {}

SipHeaders::SipHeaders(const std::unordered_map<std::string, std::string>& headers) : headers_(headers) {}

bool SipHeaders::hasHeader(const std::string& name) const {
    // Normalisasi nama header menjadi lowercase
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return headers_.find(normalized) != headers_.end();
}

std::string SipHeaders::getHeader(const std::string& name) const {
    // Normalisasi nama header menjadi lowercase
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = headers_.find(normalized);
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

void SipHeaders::setHeader(const std::string& name, const std::string& value) {
    // Normalisasi nama header menjadi lowercase
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    headers_[normalized] = value;
}

void SipHeaders::removeHeader(const std::string& name) {
    // Normalisasi nama header menjadi lowercase
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    headers_.erase(normalized);
}

std::unordered_map<std::string, std::string> SipHeaders::getAllHeaders() const {
    return headers_;
}

// Common header accessors
std::string SipHeaders::getFrom() const { return getHeader("from"); }
void SipHeaders::setFrom(const std::string& value) { setHeader("from", value); }

std::string SipHeaders::getTo() const { return getHeader("to"); }
void SipHeaders::setTo(const std::string& value) { setHeader("to", value); }

std::string SipHeaders::getCallId() const { return getHeader("call-id"); }
void SipHeaders::setCallId(const std::string& value) { setHeader("call-id", value); }

std::string SipHeaders::getCSeq() const { return getHeader("cseq"); }
void SipHeaders::setCSeq(const std::string& value) { setHeader("cseq", value); }

std::string SipHeaders::getContact() const { return getHeader("contact"); }
void SipHeaders::setContact(const std::string& value) { setHeader("contact", value); }

std::string SipHeaders::getVia() const { return getHeader("via"); }
void SipHeaders::setVia(const std::string& value) { setHeader("via", value); }

std::string SipHeaders::getContentType() const { return getHeader("content-type"); }
void SipHeaders::setContentType(const std::string& value) { setHeader("content-type", value); }

int SipHeaders::getContentLength() const {
    std::string cl = getHeader("content-length");
    if (cl.empty()) {
        return 0;
    }
    try {
        return std::stoi(cl);
    } catch (const std::exception& e) {
        core::Logger::error("Failed to parse Content-Length: {}", e.what());
        return 0;
    }
}

void SipHeaders::setContentLength(int length) {
    setHeader("content-length", std::to_string(length));
}

std::string SipHeaders::getMaxForwards() const { return getHeader("max-forwards"); }
void SipHeaders::setMaxForwards(const std::string& value) { setHeader("max-forwards", value); }

std::string SipHeaders::getExpires() const { return getHeader("expires"); }
void SipHeaders::setExpires(const std::string& value) { setHeader("expires", value); }

std::string SipHeaders::getUserAgent() const { return getHeader("user-agent"); }
void SipHeaders::setUserAgent(const std::string& value) { setHeader("user-agent", value); }

// SIP Message implementation
SipMessage::SipMessage() :
    type_(SipMessageType::Request),
    method_(SipMethod::REGISTER),
    response_code_(SipResponseCode::OK) {}

// Create a request
SipMessage SipMessage::createRequest(SipMethod method, const SipUri& requestUri) {
    SipMessage message;
    message.type_ = SipMessageType::Request;
    message.method_ = method;
    message.request_uri_ = requestUri;
    return message;
}

// Create a response
SipMessage SipMessage::createResponse(SipResponseCode code, const SipMessage& request) {
    SipMessage message;
    message.type_ = SipMessageType::Response;
    message.response_code_ = code;

    // Copy relevant headers from request to response
    message.headers_ = request.headers_;

    // Switch To/From
    message.headers_.setTo(request.headers_.getTo());
    message.headers_.setFrom(request.headers_.getFrom());

    // Copy Call-ID and CSeq
    message.headers_.setCallId(request.headers_.getCallId());
    message.headers_.setCSeq(request.headers_.getCSeq());

    // Via headers need to be preserved
    message.headers_.setVia(request.headers_.getVia());

    return message;
}

// Parse SIP message
SipMessage SipMessage::parse(const std::string& message) {
    std::istringstream iss(message);
    std::string line;

    SipMessage result;
    std::unordered_map<std::string, std::string> headers;
    std::string current_header_name;
    std::string current_header_value;
    bool in_headers = true;
    std::string body;

    // Parse first line (request or status line)
    if (std::getline(iss, line)) {
        // Menghilangkan CR jika ada
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Check if this is a request or response
        if (line.substr(0, 3) == "SIP") {
            // This is a response: SIP/2.0 200 OK
            static const std::regex response_regex(R"(SIP/2\.0 (\d+) (.+))");
            std::smatch match;
            if (std::regex_match(line, match, response_regex)) {
                result.type_ = SipMessageType::Response;
                int code = std::stoi(match[1].str());
                result.response_code_ = static_cast<SipResponseCode>(code);
                result.reason_phrase_ = match[2].str();
            } else {
                throw SipError(SipErrorCode::ParseError, "Invalid SIP response line: " + line);
            }
        } else {
            // This is a request: METHOD uri SIP/2.0
            static const std::regex request_regex(R"((\w+) (sip:.+) SIP/2\.0)");
            std::smatch match;
            if (std::regex_match(line, match, request_regex)) {
                result.type_ = SipMessageType::Request;
                std::string method_str = match[1].str();

                // Convert method string to enum
                if (method_str == "REGISTER") {
                    result.method_ = SipMethod::REGISTER;
                } else if (method_str == "INVITE") {
                    result.method_ = SipMethod::INVITE;
                } else if (method_str == "ACK") {
                    result.method_ = SipMethod::ACK;
                } else if (method_str == "BYE") {
                    result.method_ = SipMethod::BYE;
                } else if (method_str == "CANCEL") {
                    result.method_ = SipMethod::CANCEL;
                } else if (method_str == "OPTIONS") {
                    result.method_ = SipMethod::OPTIONS;
                } else if (method_str == "INFO") {
                    result.method_ = SipMethod::INFO;
                } else if (method_str == "UPDATE") {
                    result.method_ = SipMethod::UPDATE;
                } else if (method_str == "REFER") {
                    result.method_ = SipMethod::REFER;
                } else if (method_str == "SUBSCRIBE") {
                    result.method_ = SipMethod::SUBSCRIBE;
                } else if (method_str == "NOTIFY") {
                    result.method_ = SipMethod::NOTIFY;
                } else if (method_str == "MESSAGE") {
                    result.method_ = SipMethod::MESSAGE;
                } else if (method_str == "PUBLISH") {
                    result.method_ = SipMethod::PUBLISH;
                } else if (method_str == "PRACK") {
                    result.method_ = SipMethod::PRACK;
                } else {
                    throw SipError(SipErrorCode::UnsupportedMethod, "Unsupported SIP method: " + method_str);
                }

                // Parse Request-URI
                std::string uri_str = match[2].str();
                try {
                    result.request_uri_ = SipUri(uri_str);
                } catch (const SipError& e) {
                    throw SipError(SipErrorCode::ParseError, "Invalid Request-URI: " + uri_str);
                }
            } else {
                throw SipError(SipErrorCode::ParseError, "Invalid SIP request line: " + line);
            }
        }
    } else {
        throw SipError(SipErrorCode::ParseError, "Empty SIP message");
    }

    // Parse headers and body
    while (std::getline(iss, line)) {
        // Menghilangkan CR jika ada
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (in_headers) {
            if (line.empty()) {
                // Empty line indicates end of headers
                in_headers = false;
                continue;
            }

            if (line[0] == ' ' || line[0] == '\t') {
                // This is a continuation of the previous header
                current_header_value += " " + line.substr(1);
                headers[current_header_name] = current_header_value;
            } else {
                // This is a new header
                size_t colon_pos = line.find(':');
                if (colon_pos != std::string::npos) {
                    current_header_name = line.substr(0, colon_pos);

                    // Normalize header name (lowercase)
                    std::transform(current_header_name.begin(), current_header_name.end(),
                                  current_header_name.begin(),
                                  [](unsigned char c) { return std::tolower(c); });

                    // Skip whitespace after colon
                    size_t value_start = colon_pos + 1;
                    while (value_start < line.length() && std::isspace(line[value_start])) {
                        value_start++;
                    }

                    current_header_value = line.substr(value_start);
                    headers[current_header_name] = current_header_value;
                }
            }
        } else {
            // This is part of the body
            body += line + "\r\n";
        }
    }

    result.headers_ = SipHeaders(headers);
    result.body_ = body;

    return result;
}

std::string SipMessage::toString() const {
    std::ostringstream oss;

    // First line (request or status line)
    if (type_ == SipMessageType::Request) {
        // Convert method enum to string
        std::string method_str;
        switch (method_) {
            case SipMethod::REGISTER: method_str = "REGISTER"; break;
            case SipMethod::INVITE: method_str = "INVITE"; break;
            case SipMethod::ACK: method_str = "ACK"; break;
            case SipMethod::BYE: method_str = "BYE"; break;
            case SipMethod::CANCEL: method_str = "CANCEL"; break;
            case SipMethod::OPTIONS: method_str = "OPTIONS"; break;
            case SipMethod::INFO: method_str = "INFO"; break;
            case SipMethod::UPDATE: method_str = "UPDATE"; break;
            case SipMethod::REFER: method_str = "REFER"; break;
            case SipMethod::SUBSCRIBE: method_str = "SUBSCRIBE"; break;
            case SipMethod::NOTIFY: method_str = "NOTIFY"; break;
            case SipMethod::MESSAGE: method_str = "MESSAGE"; break;
            case SipMethod::PUBLISH: method_str = "PUBLISH"; break;
            case SipMethod::PRACK: method_str = "PRACK"; break;
        }

        oss << method_str << " " << request_uri_.toString() << " SIP/2.0\r\n";
    } else {
        // Response
        oss << "SIP/2.0 " << static_cast<int>(response_code_) << " " << reason_phrase_ << "\r\n";
    }

    // Headers
    auto all_headers = headers_.getAllHeaders();
    for (const auto& [name, value] : all_headers) {
        // Capitalize each word in header name for output
        std::string display_name;
        bool capitalize = true;
        for (char c : name) {
            if (capitalize && std::isalpha(c)) {
                display_name += std::toupper(c);
                capitalize = false;
            } else if (c == '-') {
                display_name += c;
                capitalize = true;
            } else {
                display_name += c;
            }
        }

        oss << display_name << ": " << value << "\r\n";
    }

    // Empty line before body
    oss << "\r\n";

    // Body (if any)
    if (!body_.empty()) {
        oss << body_;
    }

    return oss.str();
}

void SipMessage::setBody(const std::string& body, const std::string& content_type) {
    body_ = body;
    headers_.setContentLength(static_cast<int>(body.length()));
    if (!content_type.empty()) {
        headers_.setContentType(content_type);
    }
}

std::string SipMessage::generateBranch() {
    std::string branch = "z9hG4bK";  // Magic cookie for RFC 3261 compliance

    // Generate a random part
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    for (int i = 0; i < 16; i++) {
        branch += "0123456789abcdef"[dis(gen)];
    }

    return branch;
}

std::string SipMessage::generateCallId() {
    std::stringstream ss;

    // Generate a random call ID
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    for (int i = 0; i < 32; i++) {
        ss << "0123456789abcdef"[dis(gen)];
    }

    // Append a host part
    ss << "@fmus-sip";

    return ss.str();
}

std::string SipMessage::generateTag() {
    std::stringstream ss;

    // Generate a random tag
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    for (int i = 0; i < 16; i++) {
        ss << "0123456789abcdef"[dis(gen)];
    }

    return ss.str();
}

std::string SipMessage::getReasonPhrase(SipResponseCode code) {
    switch (code) {
        case SipResponseCode::Trying: return "Trying";
        case SipResponseCode::Ringing: return "Ringing";
        case SipResponseCode::OK: return "OK";
        case SipResponseCode::MovedPermanently: return "Moved Permanently";
        case SipResponseCode::BadRequest: return "Bad Request";
        case SipResponseCode::Unauthorized: return "Unauthorized";
        case SipResponseCode::Forbidden: return "Forbidden";
        case SipResponseCode::NotFound: return "Not Found";
        case SipResponseCode::MethodNotAllowed: return "Method Not Allowed";
        case SipResponseCode::RequestTimeout: return "Request Timeout";
        case SipResponseCode::Busy: return "Busy Here";
        case SipResponseCode::TemporarilyUnavailable: return "Temporarily Unavailable";
        case SipResponseCode::CallLegTransactionDoesNotExist: return "Call Leg/Transaction Does Not Exist";
        case SipResponseCode::AddressIncomplete: return "Address Incomplete";
        case SipResponseCode::RequestTerminated: return "Request Terminated";
        case SipResponseCode::NotAcceptableHere: return "Not Acceptable Here";
        case SipResponseCode::InternalServerError: return "Internal Server Error";
        case SipResponseCode::ServiceUnavailable: return "Service Unavailable";
        case SipResponseCode::ServerTimeout: return "Server Time-out";
        case SipResponseCode::BusyEverywhere: return "Busy Everywhere";
        case SipResponseCode::Decline: return "Decline";
        default: return "Unknown";
    }
}

} // namespace fmus::sip