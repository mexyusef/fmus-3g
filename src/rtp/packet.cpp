#include "fmus/rtp/packet.hpp"
#include "fmus/core/logger.hpp"
#include <cstring>

namespace fmus::rtp {

// RtpHeader implementation
std::vector<uint8_t> RtpHeader::serialize() const {
    std::vector<uint8_t> data;
    data.reserve(12 + csrc_list.size() * 4);
    
    // First byte: V(2) + P(1) + X(1) + CC(4)
    uint8_t byte0 = (version << 6) | (padding ? 0x20 : 0) | (extension ? 0x10 : 0) | csrc_count;
    data.push_back(byte0);
    
    // Second byte: M(1) + PT(7)
    uint8_t byte1 = (marker ? 0x80 : 0) | payload_type;
    data.push_back(byte1);
    
    // Sequence number (16 bits, big endian)
    data.push_back((sequence_number >> 8) & 0xFF);
    data.push_back(sequence_number & 0xFF);
    
    // Timestamp (32 bits, big endian)
    data.push_back((timestamp >> 24) & 0xFF);
    data.push_back((timestamp >> 16) & 0xFF);
    data.push_back((timestamp >> 8) & 0xFF);
    data.push_back(timestamp & 0xFF);
    
    // SSRC (32 bits, big endian)
    data.push_back((ssrc >> 24) & 0xFF);
    data.push_back((ssrc >> 16) & 0xFF);
    data.push_back((ssrc >> 8) & 0xFF);
    data.push_back(ssrc & 0xFF);
    
    // CSRC list
    for (uint32_t csrc : csrc_list) {
        data.push_back((csrc >> 24) & 0xFF);
        data.push_back((csrc >> 16) & 0xFF);
        data.push_back((csrc >> 8) & 0xFF);
        data.push_back(csrc & 0xFF);
    }
    
    return data;
}

RtpHeader RtpHeader::deserialize(const uint8_t* data, size_t size) {
    if (size < 12) {
        throw std::runtime_error("RTP header too small");
    }
    
    RtpHeader header;
    
    // Parse first byte
    uint8_t byte0 = data[0];
    header.version = (byte0 >> 6) & 0x03;
    header.padding = (byte0 & 0x20) != 0;
    header.extension = (byte0 & 0x10) != 0;
    header.csrc_count = byte0 & 0x0F;
    
    // Parse second byte
    uint8_t byte1 = data[1];
    header.marker = (byte1 & 0x80) != 0;
    header.payload_type = byte1 & 0x7F;
    
    // Parse sequence number
    header.sequence_number = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    
    // Parse timestamp
    header.timestamp = (static_cast<uint32_t>(data[4]) << 24) |
                      (static_cast<uint32_t>(data[5]) << 16) |
                      (static_cast<uint32_t>(data[6]) << 8) |
                      static_cast<uint32_t>(data[7]);
    
    // Parse SSRC
    header.ssrc = (static_cast<uint32_t>(data[8]) << 24) |
                 (static_cast<uint32_t>(data[9]) << 16) |
                 (static_cast<uint32_t>(data[10]) << 8) |
                 static_cast<uint32_t>(data[11]);
    
    // Parse CSRC list
    size_t expected_size = 12 + header.csrc_count * 4;
    if (size < expected_size) {
        throw std::runtime_error("RTP header CSRC list incomplete");
    }
    
    header.csrc_list.reserve(header.csrc_count);
    for (int i = 0; i < header.csrc_count; ++i) {
        size_t offset = 12 + i * 4;
        uint32_t csrc = (static_cast<uint32_t>(data[offset]) << 24) |
                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
                       static_cast<uint32_t>(data[offset + 3]);
        header.csrc_list.push_back(csrc);
    }
    
    return header;
}

size_t RtpHeader::getSize() const {
    return 12 + csrc_list.size() * 4;
}

// RtpPacket implementation
RtpPacket::RtpPacket(const RtpHeader& header, const std::vector<uint8_t>& payload)
    : header_(header), payload_(payload) {
}

RtpPacket::RtpPacket(const RtpHeader& header, const uint8_t* payload_data, size_t payload_size)
    : header_(header) {
    payload_.assign(payload_data, payload_data + payload_size);
}

std::vector<uint8_t> RtpPacket::serialize() const {
    std::vector<uint8_t> header_data = header_.serialize();
    std::vector<uint8_t> packet_data;
    packet_data.reserve(header_data.size() + payload_.size());
    
    packet_data.insert(packet_data.end(), header_data.begin(), header_data.end());
    packet_data.insert(packet_data.end(), payload_.begin(), payload_.end());
    
    return packet_data;
}

std::unique_ptr<RtpPacket> RtpPacket::deserialize(const uint8_t* data, size_t size) {
    if (size < 12) {
        return nullptr;
    }
    
    try {
        RtpHeader header = RtpHeader::deserialize(data, size);
        size_t header_size = header.getSize();
        
        if (size < header_size) {
            return nullptr;
        }
        
        std::vector<uint8_t> payload;
        if (size > header_size) {
            payload.assign(data + header_size, data + size);
        }
        
        return std::make_unique<RtpPacket>(header, payload);
    } catch (const std::exception& e) {
        fmus::core::Logger::error("Failed to deserialize RTP packet: {}", e.what());
        return nullptr;
    }
}

// RtcpHeader implementation
std::vector<uint8_t> RtcpHeader::serialize() const {
    std::vector<uint8_t> data(4);
    
    // First byte: V(2) + P(1) + Count(5)
    data[0] = (version << 6) | (padding ? 0x20 : 0) | (count & 0x1F);
    
    // Second byte: Packet Type
    data[1] = static_cast<uint8_t>(packet_type);
    
    // Length (16 bits, big endian)
    data[2] = (length >> 8) & 0xFF;
    data[3] = length & 0xFF;
    
    return data;
}

RtcpHeader RtcpHeader::deserialize(const uint8_t* data, size_t size) {
    if (size < 4) {
        throw std::runtime_error("RTCP header too small");
    }
    
    RtcpHeader header;
    
    // Parse first byte
    header.version = (data[0] >> 6) & 0x03;
    header.padding = (data[0] & 0x20) != 0;
    header.count = data[0] & 0x1F;
    
    // Parse packet type
    header.packet_type = static_cast<RtcpPacketType>(data[1]);
    
    // Parse length
    header.length = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    
    return header;
}

// RtcpPacket implementation
RtcpPacket::RtcpPacket(const RtcpHeader& header, const std::vector<uint8_t>& payload)
    : header_(header), payload_(payload) {
}

std::vector<uint8_t> RtcpPacket::serialize() const {
    std::vector<uint8_t> header_data = header_.serialize();
    std::vector<uint8_t> packet_data;
    packet_data.reserve(header_data.size() + payload_.size());
    
    packet_data.insert(packet_data.end(), header_data.begin(), header_data.end());
    packet_data.insert(packet_data.end(), payload_.begin(), payload_.end());
    
    return packet_data;
}

std::unique_ptr<RtcpPacket> RtcpPacket::deserialize(const uint8_t* data, size_t size) {
    if (size < 4) {
        return nullptr;
    }
    
    try {
        RtcpHeader header = RtcpHeader::deserialize(data, size);
        
        std::vector<uint8_t> payload;
        if (size > 4) {
            payload.assign(data + 4, data + size);
        }
        
        return std::make_unique<RtcpPacket>(header, payload);
    } catch (const std::exception& e) {
        fmus::core::Logger::error("Failed to deserialize RTCP packet: {}", e.what());
        return nullptr;
    }
}

} // namespace fmus::rtp
