#include "fmus/sip/sdp.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <random>

namespace fmus::sip {

// ConnectionData implementation
std::string ConnectionData::toString() const {
    return network_type + " " + address_type + " " + connection_address;
}

ConnectionData ConnectionData::fromString(const std::string& line) {
    std::istringstream iss(line);
    ConnectionData data;
    iss >> data.network_type >> data.address_type >> data.connection_address;
    return data;
}

// Origin implementation
std::string Origin::toString() const {
    return username + " " + std::to_string(session_id) + " " + 
           std::to_string(session_version) + " " + network_type + " " + 
           address_type + " " + unicast_address;
}

Origin Origin::fromString(const std::string& line) {
    std::istringstream iss(line);
    Origin origin;
    iss >> origin.username >> origin.session_id >> origin.session_version 
        >> origin.network_type >> origin.address_type >> origin.unicast_address;
    return origin;
}

// Attribute implementation
std::string Attribute::toString() const {
    if (value.empty()) {
        return name;
    }
    return name + ":" + value;
}

Attribute Attribute::fromString(const std::string& line) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return Attribute(line);
    }
    return Attribute(line.substr(0, colon_pos), line.substr(colon_pos + 1));
}

// MediaDescription implementation
MediaDescription::MediaDescription(MediaType type, uint16_t port, Protocol protocol)
    : type_(type), port_(port), protocol_(protocol) {
}

std::vector<Attribute> MediaDescription::getAttributes(const std::string& name) const {
    std::vector<Attribute> result;
    for (const auto& attr : attributes_) {
        if (attr.name == name) {
            result.push_back(attr);
        }
    }
    return result;
}

std::string MediaDescription::getAttributeValue(const std::string& name) const {
    for (const auto& attr : attributes_) {
        if (attr.name == name) {
            return attr.value;
        }
    }
    return "";
}

bool MediaDescription::hasAttribute(const std::string& name) const {
    return std::any_of(attributes_.begin(), attributes_.end(),
                      [&name](const Attribute& attr) { return attr.name == name; });
}

void MediaDescription::removeAttributes(const std::string& name) {
    attributes_.erase(
        std::remove_if(attributes_.begin(), attributes_.end(),
                      [&name](const Attribute& attr) { return attr.name == name; }),
        attributes_.end());
}

std::string MediaDescription::toString() const {
    std::ostringstream oss;
    
    // Media line: m=<media> <port> <proto> <fmt> ...
    oss << "m=" << mediaTypeToString(type_) << " " << port_ << " " 
        << protocolToString(protocol_);
    
    for (uint8_t format : formats_) {
        oss << " " << static_cast<int>(format);
    }
    oss << "\r\n";
    
    // Connection data (if present)
    if (connection_data_) {
        oss << "c=" << connection_data_->toString() << "\r\n";
    }
    
    // Attributes
    for (const auto& attr : attributes_) {
        oss << "a=" << attr.toString() << "\r\n";
    }
    
    return oss.str();
}

MediaDescription MediaDescription::fromString(const std::string& media_section) {
    MediaDescription media;
    std::istringstream iss(media_section);
    std::string line;
    
    bool first_line = true;
    while (std::getline(iss, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        if (line.length() < 2 || line[1] != '=') {
            throw std::invalid_argument("Invalid SDP line format: " + line);
        }
        
        char type = line[0];
        std::string content = line.substr(2);
        
        switch (type) {
            case 'm': {
                if (!first_line) {
                    throw std::invalid_argument("Media line must be first in media section");
                }
                
                std::istringstream media_iss(content);
                std::string media_type_str, protocol_str;
                uint16_t port;
                
                media_iss >> media_type_str >> port >> protocol_str;
                
                media.setType(stringToMediaType(media_type_str));
                media.setPort(port);
                media.setProtocol(stringToProtocol(protocol_str));
                
                // Parse formats
                int format;
                while (media_iss >> format) {
                    media.addFormat(static_cast<uint8_t>(format));
                }
                break;
            }
            case 'c':
                media.setConnectionData(ConnectionData::fromString(content));
                break;
            case 'a':
                media.addAttribute(Attribute::fromString(content));
                break;
            default:
                core::Logger::warn("Ignoring unknown media-level SDP line: {}", line);
                break;
        }
        
        first_line = false;
    }
    
    return media;
}

// SessionDescription implementation
SessionDescription::SessionDescription() {
    // Set default origin with random session ID
    std::random_device rd;
    std::mt19937_64 gen(rd());
    
    origin_.username = "fmus";
    origin_.session_id = gen();
    origin_.session_version = gen();
    origin_.unicast_address = "127.0.0.1";
    
    // Default timing (permanent session)
    timing_ = {0, 0};
}

std::vector<Attribute> SessionDescription::getAttributes(const std::string& name) const {
    std::vector<Attribute> result;
    for (const auto& attr : attributes_) {
        if (attr.name == name) {
            result.push_back(attr);
        }
    }
    return result;
}

std::string SessionDescription::getAttributeValue(const std::string& name) const {
    for (const auto& attr : attributes_) {
        if (attr.name == name) {
            return attr.value;
        }
    }
    return "";
}

bool SessionDescription::hasAttribute(const std::string& name) const {
    return std::any_of(attributes_.begin(), attributes_.end(),
                      [&name](const Attribute& attr) { return attr.name == name; });
}

void SessionDescription::removeAttributes(const std::string& name) {
    attributes_.erase(
        std::remove_if(attributes_.begin(), attributes_.end(),
                      [&name](const Attribute& attr) { return attr.name == name; }),
        attributes_.end());
}

std::vector<MediaDescription> SessionDescription::getMediaByType(MediaType type) const {
    std::vector<MediaDescription> result;
    for (const auto& media : media_descriptions_) {
        if (media.getType() == type) {
            result.push_back(media);
        }
    }
    return result;
}

void SessionDescription::removeMediaByType(MediaType type) {
    media_descriptions_.erase(
        std::remove_if(media_descriptions_.begin(), media_descriptions_.end(),
                      [type](const MediaDescription& media) { return media.getType() == type; }),
        media_descriptions_.end());
}

std::string SessionDescription::toString() const {
    std::ostringstream oss;
    
    // Version
    oss << "v=" << version_ << "\r\n";
    
    // Origin
    oss << "o=" << origin_.toString() << "\r\n";
    
    // Session name
    oss << "s=" << session_name_ << "\r\n";
    
    // Session information (optional)
    if (!session_info_.empty()) {
        oss << "i=" << session_info_ << "\r\n";
    }
    
    // URI (optional)
    if (!uri_.empty()) {
        oss << "u=" << uri_ << "\r\n";
    }
    
    // Email (optional)
    if (!email_.empty()) {
        oss << "e=" << email_ << "\r\n";
    }
    
    // Phone (optional)
    if (!phone_.empty()) {
        oss << "p=" << phone_ << "\r\n";
    }
    
    // Connection data (session-level)
    if (connection_data_) {
        oss << "c=" << connection_data_->toString() << "\r\n";
    }
    
    // Bandwidth (optional)
    if (!bandwidth_.empty()) {
        oss << "b=" << bandwidth_ << "\r\n";
    }
    
    // Timing
    oss << "t=" << timing_.toString() << "\r\n";
    
    // Session-level attributes
    for (const auto& attr : attributes_) {
        oss << "a=" << attr.toString() << "\r\n";
    }
    
    // Media descriptions
    for (const auto& media : media_descriptions_) {
        oss << media.toString();
    }
    
    return oss.str();
}

SessionDescription SessionDescription::fromString(const std::string& sdp) {
    SessionDescription session;
    std::istringstream iss(sdp);
    std::string line;

    std::string current_media_section;
    bool in_media_section = false;

    while (std::getline(iss, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) continue;

        if (line.length() < 2 || line[1] != '=') {
            throw std::invalid_argument("Invalid SDP line format: " + line);
        }

        char type = line[0];
        std::string content = line.substr(2);

        // If we encounter a new media section, process the previous one
        if (type == 'm' && in_media_section) {
            session.addMedia(MediaDescription::fromString(current_media_section));
            current_media_section.clear();
        }

        // Handle media-level lines
        if (type == 'm' || type == 'c' || type == 'a') {
            if (type == 'm') {
                in_media_section = true;
            }

            if (in_media_section) {
                current_media_section += line + "\r\n";
                continue;
            }
        }

        // Handle session-level lines
        switch (type) {
            case 'v':
                // Version is always 0, ignore
                break;
            case 'o':
                session.setOrigin(Origin::fromString(content));
                break;
            case 's':
                session.setSessionName(content);
                break;
            case 'i':
                session.setSessionInfo(content);
                break;
            case 'u':
                session.setUri(content);
                break;
            case 'e':
                session.setEmail(content);
                break;
            case 'p':
                session.setPhone(content);
                break;
            case 'c':
                if (!in_media_section) {
                    session.setConnectionData(ConnectionData::fromString(content));
                }
                break;
            case 'b':
                session.setBandwidth(content);
                break;
            case 't': {
                std::istringstream timing_iss(content);
                uint64_t start, stop;
                timing_iss >> start >> stop;
                session.setTiming(start, stop);
                break;
            }
            case 'a':
                if (!in_media_section) {
                    session.addAttribute(Attribute::fromString(content));
                }
                break;
            default:
                core::Logger::warn("Ignoring unknown session-level SDP line: {}", line);
                break;
        }
    }

    // Process the last media section if any
    if (in_media_section && !current_media_section.empty()) {
        session.addMedia(MediaDescription::fromString(current_media_section));
    }

    return session;
}

bool SessionDescription::isValid() const {
    return validate().empty();
}

std::vector<std::string> SessionDescription::validate() const {
    std::vector<std::string> errors;

    // Check required fields
    if (origin_.username.empty()) {
        errors.push_back("Origin username is required");
    }

    if (session_name_.empty()) {
        errors.push_back("Session name is required");
    }

    // Check connection data
    bool has_session_connection = connection_data_.has_value();
    bool all_media_have_connection = true;

    for (const auto& media : media_descriptions_) {
        if (!media.hasConnectionData()) {
            all_media_have_connection = false;
            break;
        }
    }

    if (!has_session_connection && !all_media_have_connection) {
        errors.push_back("Connection data required at session or media level");
    }

    // Validate media descriptions
    for (const auto& media : media_descriptions_) {
        if (media.getFormats().empty()) {
            errors.push_back("Media description must have at least one format");
        }
    }

    return errors;
}

// Utility functions
std::string mediaTypeToString(MediaType type) {
    switch (type) {
        case MediaType::AUDIO: return "audio";
        case MediaType::VIDEO: return "video";
        case MediaType::APPLICATION: return "application";
        case MediaType::DATA: return "data";
        case MediaType::CONTROL: return "control";
        default: return "unknown";
    }
}

MediaType stringToMediaType(const std::string& type) {
    if (type == "audio") return MediaType::AUDIO;
    if (type == "video") return MediaType::VIDEO;
    if (type == "application") return MediaType::APPLICATION;
    if (type == "data") return MediaType::DATA;
    if (type == "control") return MediaType::CONTROL;
    throw std::invalid_argument("Unknown media type: " + type);
}

std::string protocolToString(Protocol protocol) {
    switch (protocol) {
        case Protocol::RTP_AVP: return "RTP/AVP";
        case Protocol::RTP_SAVP: return "RTP/SAVP";
        case Protocol::RTP_AVPF: return "RTP/AVPF";
        case Protocol::RTP_SAVPF: return "RTP/SAVPF";
        case Protocol::UDP: return "udp";
        case Protocol::TCP: return "tcp";
        default: return "unknown";
    }
}

Protocol stringToProtocol(const std::string& protocol) {
    if (protocol == "RTP/AVP") return Protocol::RTP_AVP;
    if (protocol == "RTP/SAVP") return Protocol::RTP_SAVP;
    if (protocol == "RTP/AVPF") return Protocol::RTP_AVPF;
    if (protocol == "RTP/SAVPF") return Protocol::RTP_SAVPF;
    if (protocol == "udp") return Protocol::UDP;
    if (protocol == "tcp") return Protocol::TCP;
    throw std::invalid_argument("Unknown protocol: " + protocol);
}

// SdpBuilder implementation
SessionDescription SdpBuilder::createBasicAudioOffer(
    const std::string& session_id,
    const std::string& local_ip,
    uint16_t audio_port,
    const std::vector<uint8_t>& audio_codecs) {

    SessionDescription sdp;

    // Set origin
    Origin origin;
    origin.username = "fmus";
    origin.session_id = std::stoull(session_id);
    origin.session_version = origin.session_id;
    origin.unicast_address = local_ip;
    sdp.setOrigin(origin);

    // Set session name
    sdp.setSessionName("FMUS Audio Session");

    // Set connection data
    ConnectionData conn;
    conn.connection_address = local_ip;
    sdp.setConnectionData(conn);

    // Create audio media
    MediaDescription audio(MediaType::AUDIO, audio_port, Protocol::RTP_AVP);
    audio.setFormats(audio_codecs);

    // Add common audio attributes
    for (uint8_t codec : audio_codecs) {
        switch (codec) {
            case 0:
                audio.addAttribute("rtpmap", "0 PCMU/8000");
                break;
            case 8:
                audio.addAttribute("rtpmap", "8 PCMA/8000");
                break;
            case 9:
                audio.addAttribute("rtpmap", "9 G722/8000");
                break;
            case 18:
                audio.addAttribute("rtpmap", "18 G729/8000");
                break;
            default:
                // Dynamic payload type
                if (codec >= 96) {
                    audio.addAttribute("rtpmap", std::to_string(codec) + " opus/48000/2");
                }
                break;
        }
    }

    audio.addAttribute("sendrecv");
    sdp.addMedia(audio);

    return sdp;
}

SessionDescription SdpBuilder::createBasicVideoOffer(
    const std::string& session_id,
    const std::string& local_ip,
    uint16_t audio_port,
    uint16_t video_port,
    const std::vector<uint8_t>& audio_codecs,
    const std::vector<uint8_t>& video_codecs) {

    // Start with audio offer
    SessionDescription sdp = createBasicAudioOffer(session_id, local_ip, audio_port, audio_codecs);

    // Update session name
    sdp.setSessionName("FMUS Audio/Video Session");

    // Create video media
    MediaDescription video(MediaType::VIDEO, video_port, Protocol::RTP_AVP);
    video.setFormats(video_codecs);

    // Add common video attributes
    for (uint8_t codec : video_codecs) {
        switch (codec) {
            case 96:
                video.addAttribute("rtpmap", "96 H264/90000");
                video.addAttribute("fmtp", "96 profile-level-id=42e01e");
                break;
            case 97:
                video.addAttribute("rtpmap", "97 VP8/90000");
                break;
            case 98:
                video.addAttribute("rtpmap", "98 VP9/90000");
                break;
            default:
                // Dynamic payload type
                if (codec >= 96) {
                    video.addAttribute("rtpmap", std::to_string(codec) + " H264/90000");
                }
                break;
        }
    }

    video.addAttribute("sendrecv");
    sdp.addMedia(video);

    return sdp;
}

SessionDescription SdpBuilder::createAnswer(
    const SessionDescription& offer,
    const std::string& local_ip,
    uint16_t audio_port,
    uint16_t video_port) {

    SessionDescription answer = offer;

    // Update origin with new session version
    Origin origin = answer.getOrigin();
    origin.session_version++;
    origin.unicast_address = local_ip;
    answer.setOrigin(origin);

    // Update connection data
    ConnectionData conn;
    conn.connection_address = local_ip;
    answer.setConnectionData(conn);

    // Update media ports
    auto& media_descriptions = answer.getMediaDescriptions();
    for (auto& media : media_descriptions) {
        switch (media.getType()) {
            case MediaType::AUDIO:
                if (audio_port > 0) {
                    media.setPort(audio_port);
                } else {
                    media.setPort(0); // Reject audio
                    media.removeAttributes("sendrecv");
                    media.removeAttributes("recvonly");
                    media.removeAttributes("sendonly");
                    media.addAttribute("inactive");
                }
                break;
            case MediaType::VIDEO:
                if (video_port > 0) {
                    media.setPort(video_port);
                } else {
                    media.setPort(0); // Reject video
                    media.removeAttributes("sendrecv");
                    media.removeAttributes("recvonly");
                    media.removeAttributes("sendonly");
                    media.addAttribute("inactive");
                }
                break;
            default:
                // Reject other media types
                media.setPort(0);
                media.addAttribute("inactive");
                break;
        }
    }

    return answer;
}

} // namespace fmus::sip
