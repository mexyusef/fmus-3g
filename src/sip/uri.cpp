#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>

#include <regex>
#include <sstream>
#include <algorithm>

namespace fmus::sip {

// SipError implementation
SipError::SipError(SipErrorCode code, const std::string& message)
    : Error(core::ErrorCode::SipError, message), code_(code) {}

// SipUri implementation
SipUri::SipUri(const std::string& uri) {
    // Parsing SIP URI according to RFC 3261
    // sip:user:password@host:port;uri-parameters?headers

    // Regex untuk memecah URI menjadi bagian-bagian
    static const std::regex uri_regex(
        R"(^(sip|sips):(?:([^:@]+)(?::([^@]+))?@)?([^:;?]+)(?::(\d+))?(?:;([^?]+))?(?:\?(.+))?$)",
        std::regex::ECMAScript
    );

    std::smatch match;
    if (std::regex_match(uri, match, uri_regex)) {
        // Scheme (sip or sips)
        scheme_ = match[1].str();

        // User
        if (match[2].matched) {
            user_ = match[2].str();
        }

        // Host
        host_ = match[4].str();

        // Port
        if (match[5].matched && !match[5].str().empty()) {
            try {
                port_ = static_cast<uint16_t>(std::stoi(match[5].str()));
            } catch (const std::exception& e) {
                port_ = 0;
                core::Logger::error("Failed to parse port in SIP URI: {}", e.what());
            }
        }

        // URI parameters
        if (match[6].matched) {
            std::string params = match[6].str();
            std::string::size_type pos = 0;
            std::string::size_type prev = 0;

            // Memecah string parameter berdasarkan ';'
            while ((pos = params.find(';', prev)) != std::string::npos) {
                parseParameter(params.substr(prev, pos - prev));
                prev = pos + 1;
            }

            // Parameter terakhir atau satu-satunya
            parseParameter(params.substr(prev));
        }
    } else {
        // Invalid URI
        core::Logger::error("Invalid SIP URI format: {}", uri);
        throw SipError(SipErrorCode::InvalidMessage, "Invalid SIP URI format: " + uri);
    }
}

SipUri::SipUri(const std::string& user, const std::string& host, uint16_t port)
    : user_(user), host_(host), port_(port) {
}

std::string SipUri::toString() const {
    std::ostringstream oss;
    oss << scheme_ << ":";

    if (!user_.empty()) {
        oss << user_ << "@";
    }

    oss << host_;

    if (port_ > 0) {
        oss << ":" << port_;
    }

    // Menambahkan parameters
    for (const auto& param : parameters_) {
        oss << ";" << param.first;
        if (!param.second.empty()) {
            oss << "=" << param.second;
        }
    }

    return oss.str();
}

// Helper to parse parameter in format name=value or just name
void SipUri::parseParameter(const std::string& param) {
    std::string::size_type pos = param.find('=');
    if (pos != std::string::npos) {
        std::string name = param.substr(0, pos);
        std::string value = param.substr(pos + 1);
        parameters_[name] = value;
    } else {
        parameters_[param] = "";
    }
}

} // namespace fmus::sip