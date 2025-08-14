#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>

namespace fmus::sip {

// SDP Media Types
enum class MediaType {
    AUDIO,
    VIDEO,
    APPLICATION,
    DATA,
    CONTROL
};

// SDP Protocol Types
enum class Protocol {
    RTP_AVP,      // RTP/AVP
    RTP_SAVP,     // RTP/SAVP (Secure)
    RTP_AVPF,     // RTP/AVPF (Feedback)
    RTP_SAVPF,    // RTP/SAVPF (Secure + Feedback)
    UDP,
    TCP
};

// SDP Connection Data
struct ConnectionData {
    std::string network_type = "IN";  // Internet
    std::string address_type = "IP4"; // IPv4 or IP6
    std::string connection_address;
    
    std::string toString() const;
    static ConnectionData fromString(const std::string& line);
};

// SDP Origin
struct Origin {
    std::string username;
    uint64_t session_id;
    uint64_t session_version;
    std::string network_type = "IN";
    std::string address_type = "IP4";
    std::string unicast_address;
    
    std::string toString() const;
    static Origin fromString(const std::string& line);
};

// SDP Attribute
struct Attribute {
    std::string name;
    std::string value;
    
    Attribute() = default;
    Attribute(const std::string& name, const std::string& value = "") 
        : name(name), value(value) {}
    
    std::string toString() const;
    static Attribute fromString(const std::string& line);
    
    bool isProperty() const { return value.empty(); }
};

// SDP Media Description
class MediaDescription {
public:
    MediaDescription() = default;
    MediaDescription(MediaType type, uint16_t port, Protocol protocol);
    
    // Basic properties
    MediaType getType() const { return type_; }
    void setType(MediaType type) { type_ = type; }
    
    uint16_t getPort() const { return port_; }
    void setPort(uint16_t port) { port_ = port; }
    
    Protocol getProtocol() const { return protocol_; }
    void setProtocol(Protocol protocol) { protocol_ = protocol; }
    
    const std::vector<uint8_t>& getFormats() const { return formats_; }
    void addFormat(uint8_t format) { formats_.push_back(format); }
    void setFormats(const std::vector<uint8_t>& formats) { formats_ = formats; }
    
    // Connection data (optional, inherits from session if not set)
    bool hasConnectionData() const { return connection_data_.has_value(); }
    const ConnectionData& getConnectionData() const { return connection_data_.value(); }
    void setConnectionData(const ConnectionData& data) { connection_data_ = data; }
    void clearConnectionData() { connection_data_.reset(); }
    
    // Attributes
    void addAttribute(const Attribute& attr) { attributes_.push_back(attr); }
    void addAttribute(const std::string& name, const std::string& value = "") {
        attributes_.emplace_back(name, value);
    }
    
    std::vector<Attribute> getAttributes(const std::string& name) const;
    std::string getAttributeValue(const std::string& name) const;
    bool hasAttribute(const std::string& name) const;
    void removeAttributes(const std::string& name);
    
    const std::vector<Attribute>& getAllAttributes() const { return attributes_; }
    
    // Serialization
    std::string toString() const;
    static MediaDescription fromString(const std::string& media_section);

private:
    MediaType type_ = MediaType::AUDIO;
    uint16_t port_ = 0;
    Protocol protocol_ = Protocol::RTP_AVP;
    std::vector<uint8_t> formats_;
    std::optional<ConnectionData> connection_data_;
    std::vector<Attribute> attributes_;
};

// Main SDP Session Description
class SessionDescription {
public:
    SessionDescription();
    
    // Version (always 0)
    int getVersion() const { return version_; }
    
    // Origin
    const Origin& getOrigin() const { return origin_; }
    void setOrigin(const Origin& origin) { origin_ = origin; }
    
    // Session Name
    const std::string& getSessionName() const { return session_name_; }
    void setSessionName(const std::string& name) { session_name_ = name; }
    
    // Session Information (optional)
    const std::string& getSessionInfo() const { return session_info_; }
    void setSessionInfo(const std::string& info) { session_info_ = info; }
    
    // URI (optional)
    const std::string& getUri() const { return uri_; }
    void setUri(const std::string& uri) { uri_ = uri; }
    
    // Email (optional)
    const std::string& getEmail() const { return email_; }
    void setEmail(const std::string& email) { email_ = email; }
    
    // Phone (optional)
    const std::string& getPhone() const { return phone_; }
    void setPhone(const std::string& phone) { phone_ = phone; }
    
    // Connection Data (session-level)
    bool hasConnectionData() const { return connection_data_.has_value(); }
    const ConnectionData& getConnectionData() const { return connection_data_.value(); }
    void setConnectionData(const ConnectionData& data) { connection_data_ = data; }
    void clearConnectionData() { connection_data_.reset(); }
    
    // Bandwidth (optional)
    const std::string& getBandwidth() const { return bandwidth_; }
    void setBandwidth(const std::string& bandwidth) { bandwidth_ = bandwidth; }
    
    // Timing
    struct Timing {
        uint64_t start_time = 0;
        uint64_t stop_time = 0;
        
        std::string toString() const {
            return std::to_string(start_time) + " " + std::to_string(stop_time);
        }
    };
    
    const Timing& getTiming() const { return timing_; }
    void setTiming(const Timing& timing) { timing_ = timing; }
    void setTiming(uint64_t start, uint64_t stop) { timing_ = {start, stop}; }
    
    // Session-level attributes
    void addAttribute(const Attribute& attr) { attributes_.push_back(attr); }
    void addAttribute(const std::string& name, const std::string& value = "") {
        attributes_.emplace_back(name, value);
    }
    
    std::vector<Attribute> getAttributes(const std::string& name) const;
    std::string getAttributeValue(const std::string& name) const;
    bool hasAttribute(const std::string& name) const;
    void removeAttributes(const std::string& name);
    
    const std::vector<Attribute>& getAllAttributes() const { return attributes_; }
    
    // Media descriptions
    void addMedia(const MediaDescription& media) { media_descriptions_.push_back(media); }
    const std::vector<MediaDescription>& getMediaDescriptions() const { return media_descriptions_; }
    std::vector<MediaDescription>& getMediaDescriptions() { return media_descriptions_; }
    
    std::vector<MediaDescription> getMediaByType(MediaType type) const;
    void removeMediaByType(MediaType type);
    
    // Serialization
    std::string toString() const;
    static SessionDescription fromString(const std::string& sdp);
    
    // Validation
    bool isValid() const;
    std::vector<std::string> validate() const;

private:
    int version_ = 0;
    Origin origin_;
    std::string session_name_ = "-";
    std::string session_info_;
    std::string uri_;
    std::string email_;
    std::string phone_;
    std::optional<ConnectionData> connection_data_;
    std::string bandwidth_;
    Timing timing_;
    std::vector<Attribute> attributes_;
    std::vector<MediaDescription> media_descriptions_;
};

// Utility functions
std::string mediaTypeToString(MediaType type);
MediaType stringToMediaType(const std::string& type);

std::string protocolToString(Protocol protocol);
Protocol stringToProtocol(const std::string& protocol);

// Common SDP builders
class SdpBuilder {
public:
    static SessionDescription createBasicAudioOffer(
        const std::string& session_id,
        const std::string& local_ip,
        uint16_t audio_port,
        const std::vector<uint8_t>& audio_codecs = {0, 8} // PCMU, PCMA
    );
    
    static SessionDescription createBasicVideoOffer(
        const std::string& session_id,
        const std::string& local_ip,
        uint16_t audio_port,
        uint16_t video_port,
        const std::vector<uint8_t>& audio_codecs = {0, 8},
        const std::vector<uint8_t>& video_codecs = {96} // Dynamic payload
    );
    
    static SessionDescription createAnswer(
        const SessionDescription& offer,
        const std::string& local_ip,
        uint16_t audio_port,
        uint16_t video_port = 0
    );
};

} // namespace fmus::sip
