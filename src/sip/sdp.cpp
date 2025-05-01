#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>

#include <sstream>
#include <regex>
#include <chrono>
#include <random>
#include <unordered_map>

namespace fmus::sip {

// SDP Session implementation
SdpSession SdpSession::parse(const std::string& sdp) {
    SdpSession session;

    // Menggunakan regex untuk memparse SDP line-by-line
    static const std::regex line_regex(R"(^([a-z])=(.*)$)");

    // Maps to store media-specific attributes
    std::unordered_map<std::string, std::vector<std::string>> media_attrs;
    std::string current_media;

    std::istringstream iss(sdp);
    std::string line;
    while (std::getline(iss, line)) {
        // Menghilangkan CR jika ada
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::smatch match;
        if (std::regex_match(line, match, line_regex)) {
            char type = match[1].str()[0];
            std::string value = match[2].str();

            switch (type) {
                case 'v': // Version
                    // Always 0 in SDP
                    break;

                case 'o': // Origin
                    // o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
                    {
                        std::regex origin_regex(R"(^(\S+) (\d+) \d+ \S+ \S+ (\S+)$)");
                        std::smatch o_match;
                        if (std::regex_match(value, o_match, origin_regex)) {
                            session.setUsername(o_match[1].str());
                            session.setSessionId(o_match[2].str());
                            session.setConnectionAddress(o_match[3].str());
                        }
                    }
                    break;

                case 's': // Session Name
                    session.setSessionName(value);
                    break;

                case 'c': // Connection Information
                    // c=<nettype> <addrtype> <connection-address>
                    {
                        std::regex conn_regex(R"(^\S+ \S+ (\S+)$)");
                        std::smatch c_match;
                        if (std::regex_match(value, c_match, conn_regex)) {
                            session.setConnectionAddress(c_match[1].str());
                        }
                    }
                    break;

                case 'm': // Media Description
                    // m=<media> <port> <proto> <fmt> ...
                    {
                        std::regex media_regex(R"(^(\w+) (\d+) \S+ (.+)$)");
                        std::smatch m_match;
                        if (std::regex_match(value, m_match, media_regex)) {
                            std::string media_type = m_match[1].str();
                            int port = std::stoi(m_match[2].str());
                            std::string formats = m_match[3].str();

                            current_media = media_type;
                            media_attrs[current_media] = {};

                            if (media_type == "audio") {
                                session.setAudioPort(port);

                                // Parse payload types
                                std::istringstream fmt_ss(formats);
                                std::string pt;
                                while (fmt_ss >> pt) {
                                    try {
                                        int pt_num = std::stoi(pt);
                                        session.addAudioPayloadType(static_cast<rtp::RtpPayloadType>(pt_num));
                                    } catch (const std::exception& e) {
                                        core::Logger::error("Failed to parse audio payload type: {}", e.what());
                                    }
                                }
                            } else if (media_type == "video") {
                                session.setVideoPort(port);

                                // Parse payload types
                                std::istringstream fmt_ss(formats);
                                std::string pt;
                                while (fmt_ss >> pt) {
                                    try {
                                        int pt_num = std::stoi(pt);
                                        session.addVideoPayloadType(static_cast<rtp::RtpPayloadType>(pt_num));
                                    } catch (const std::exception& e) {
                                        core::Logger::error("Failed to parse video payload type: {}", e.what());
                                    }
                                }
                            }
                        }
                    }
                    break;

                case 'a': // Attribute
                    if (!current_media.empty()) {
                        media_attrs[current_media].push_back(value);
                    }
                    break;

                default:
                    // Ignore other types for now
                    break;
            }
        }
    }

    return session;
}

std::string SdpSession::toString() const {
    std::ostringstream oss;

    // Generate a random session version
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 1000000);
    int session_version = dis(gen);

    // Current time in NTP format (seconds since 1900-01-01)
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);

    // Session version is current timestamp
    std::string sess_id = session_id_.empty() ?
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count()) :
        session_id_;

    // Protocol version (always 0)
    oss << "v=0\r\n";

    // Origin line
    // o=<username> <sess-id> <sess-version> IN IP4 <unicast-address>
    oss << "o=" << username_ << " " << sess_id << " " << session_version
        << " IN IP4 " << connection_address_ << "\r\n";

    // Session name
    oss << "s=" << session_name_ << "\r\n";

    // Connection information
    oss << "c=IN IP4 " << connection_address_ << "\r\n";

    // Time description (t=<start-time> <stop-time>)
    // Use 0 0 for "session is permanent"
    oss << "t=0 0\r\n";

    // Audio media description if port is set
    if (audio_port_ > 0 && !audio_payload_types_.empty()) {
        oss << "m=audio " << audio_port_ << " RTP/AVP";
        for (auto pt : audio_payload_types_) {
            oss << " " << static_cast<int>(pt);
        }
        oss << "\r\n";

        // Add standard attributes for common payload types
        for (auto pt : audio_payload_types_) {
            switch (pt) {
                case rtp::RtpPayloadType::PCMU:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " PCMU/8000/1\r\n";
                    break;
                case rtp::RtpPayloadType::PCMA:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " PCMA/8000/1\r\n";
                    break;
                case rtp::RtpPayloadType::G722:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " G722/8000/1\r\n";
                    break;
                case rtp::RtpPayloadType::OPUS:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " opus/48000/2\r\n";
                    oss << "a=fmtp:" << static_cast<int>(pt) << " minptime=10;useinbandfec=1\r\n";
                    break;
                default:
                    // For other payload types, we'd need more information to define rtpmap
                    break;
            }
        }

        // Standard attributes
        oss << "a=sendrecv\r\n";
        oss << "a=ptime:20\r\n";
    }

    // Video media description if port is set
    if (video_port_ > 0 && !video_payload_types_.empty()) {
        oss << "m=video " << video_port_ << " RTP/AVP";
        for (auto pt : video_payload_types_) {
            oss << " " << static_cast<int>(pt);
        }
        oss << "\r\n";

        // Add standard attributes for common payload types
        for (auto pt : video_payload_types_) {
            switch (pt) {
                case rtp::RtpPayloadType::H264:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " H264/90000\r\n";
                    oss << "a=fmtp:" << static_cast<int>(pt)
                        << " profile-level-id=42e01f;packetization-mode=1\r\n";
                    break;
                case rtp::RtpPayloadType::H263:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " H263/90000\r\n";
                    break;
                case rtp::RtpPayloadType::VP8:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " VP8/90000\r\n";
                    break;
                case rtp::RtpPayloadType::VP9:
                    oss << "a=rtpmap:" << static_cast<int>(pt) << " VP9/90000\r\n";
                    break;
                default:
                    // For other payload types, we'd need more information to define rtpmap
                    break;
            }
        }

        // Standard attributes
        oss << "a=sendrecv\r\n";
    }

    return oss.str();
}

} // namespace fmus::sip