#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace fmus::rtp {

struct RtpHeader {
    uint8_t version = 2;
    bool padding = false;
    bool extension = false;
    uint8_t csrc_count = 0;
    bool marker = false;
    uint8_t payload_type = 0;
    uint16_t sequence_number = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    std::vector<uint32_t> csrc_list;
    
    // Serialize to bytes
    std::vector<uint8_t> serialize() const;
    
    // Deserialize from bytes
    static RtpHeader deserialize(const uint8_t* data, size_t size);
    
    // Get header size in bytes
    size_t getSize() const;
};

class RtpPacket {
public:
    RtpPacket() = default;
    RtpPacket(const RtpHeader& header, const std::vector<uint8_t>& payload);
    RtpPacket(const RtpHeader& header, const uint8_t* payload_data, size_t payload_size);
    
    const RtpHeader& getHeader() const { return header_; }
    RtpHeader& getHeader() { return header_; }
    
    const std::vector<uint8_t>& getPayload() const { return payload_; }
    std::vector<uint8_t>& getPayload() { return payload_; }
    
    void setPayload(const std::vector<uint8_t>& payload) { payload_ = payload; }
    void setPayload(const uint8_t* data, size_t size) { 
        payload_.assign(data, data + size); 
    }
    
    // Serialize entire packet to bytes
    std::vector<uint8_t> serialize() const;
    
    // Deserialize packet from bytes
    static std::unique_ptr<RtpPacket> deserialize(const uint8_t* data, size_t size);
    
    // Get total packet size
    size_t getSize() const { return header_.getSize() + payload_.size(); }

private:
    RtpHeader header_;
    std::vector<uint8_t> payload_;
};

// RTCP packet types
enum class RtcpPacketType : uint8_t {
    SR = 200,   // Sender Report
    RR = 201,   // Receiver Report
    SDES = 202, // Source Description
    BYE = 203,  // Goodbye
    APP = 204   // Application-defined
};

struct RtcpHeader {
    uint8_t version = 2;
    bool padding = false;
    uint8_t count = 0;
    RtcpPacketType packet_type = RtcpPacketType::SR;
    uint16_t length = 0; // Length in 32-bit words minus one
    
    std::vector<uint8_t> serialize() const;
    static RtcpHeader deserialize(const uint8_t* data, size_t size);
    size_t getSize() const { return 4; } // RTCP header is always 4 bytes
};

class RtcpPacket {
public:
    RtcpPacket() = default;
    RtcpPacket(const RtcpHeader& header, const std::vector<uint8_t>& payload);
    
    const RtcpHeader& getHeader() const { return header_; }
    RtcpHeader& getHeader() { return header_; }
    
    const std::vector<uint8_t>& getPayload() const { return payload_; }
    std::vector<uint8_t>& getPayload() { return payload_; }
    
    std::vector<uint8_t> serialize() const;
    static std::unique_ptr<RtcpPacket> deserialize(const uint8_t* data, size_t size);
    
    size_t getSize() const { return header_.getSize() + payload_.size(); }

private:
    RtcpHeader header_;
    std::vector<uint8_t> payload_;
};

} // namespace fmus::rtp
