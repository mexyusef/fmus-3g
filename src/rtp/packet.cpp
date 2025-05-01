#include <fmus/rtp/rtp.hpp>
#include <fmus/core/logger.hpp>

#include <cassert>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>  // For htonl, ntohl, etc.

namespace fmus::rtp {

// RtpError implementation
RtpError::RtpError(RtpErrorCode code, const std::string& message)
    : Error(core::ErrorCode::RtpError, message), code_(code) {}

// RtpPacket implementation
RtpPacket::RtpPacket() {
    // Default RTP version 2
    header_.version = 2;
    header_.padding = false;
    header_.extension = false;
    header_.csrcCount = 0;
    header_.marker = false;
    header_.payloadType = 0;
    header_.sequenceNumber = 0;
    header_.timestamp = 0;
    header_.ssrc = 0;
}

RtpPacket::RtpPacket(const RtpHeader& header)
    : header_(header) {
}

RtpPacket::RtpPacket(const RtpHeader& header, const void* payload, size_t payload_size)
    : header_(header) {
    setPayload(payload, payload_size);
}

void RtpPacket::setPayload(const void* data, size_t size) {
    payload_.resize(size);
    if (size > 0) {
        std::memcpy(payload_.data(), data, size);
    }
}

std::vector<uint8_t> RtpPacket::serialize() const {
    // Calculate total size
    // RTP header is 12 bytes + 4 bytes per CSRC
    size_t header_size = 12 + header_.csrc.size() * 4;
    size_t total_size = header_size + payload_.size();

    // Create buffer
    std::vector<uint8_t> buffer(total_size);

    // Serialize header
    // First byte: [V V P X C C C C]
    buffer[0] = (header_.version << 6) |
               (header_.padding ? (1 << 5) : 0) |
               (header_.extension ? (1 << 4) : 0) |
               (header_.csrcCount & 0x0F);

    // Second byte: [M P P P P P P P]
    buffer[1] = (header_.marker ? (1 << 7) : 0) |
               (header_.payloadType & 0x7F);

    // Sequence number (16 bits)
    uint16_t seq = htons(header_.sequenceNumber);
    std::memcpy(&buffer[2], &seq, 2);

    // Timestamp (32 bits)
    uint32_t ts = htonl(header_.timestamp);
    std::memcpy(&buffer[4], &ts, 4);

    // SSRC (32 bits)
    uint32_t ssrc = htonl(header_.ssrc);
    std::memcpy(&buffer[8], &ssrc, 4);

    // CSRC list
    for (size_t i = 0; i < header_.csrc.size(); ++i) {
        uint32_t csrc = htonl(header_.csrc[i]);
        std::memcpy(&buffer[12 + i * 4], &csrc, 4);
    }

    // Copy payload
    if (!payload_.empty()) {
        std::memcpy(&buffer[header_size], payload_.data(), payload_.size());
    }

    return buffer;
}

RtpPacket RtpPacket::deserialize(const uint8_t* data, size_t size) {
    // Verify minimum size (RTP header is at least 12 bytes)
    if (size < 12) {
        throw RtpError(RtpErrorCode::InvalidPacket, "Packet too small");
    }

    RtpHeader header;

    // Parse first byte: [V V P X C C C C]
    header.version = (data[0] >> 6) & 0x03;
    header.padding = ((data[0] >> 5) & 0x01) != 0;
    header.extension = ((data[0] >> 4) & 0x01) != 0;
    header.csrcCount = data[0] & 0x0F;

    // Parse second byte: [M P P P P P P P]
    header.marker = ((data[1] >> 7) & 0x01) != 0;
    header.payloadType = data[1] & 0x7F;

    // Parse sequence number (16 bits)
    uint16_t seq;
    std::memcpy(&seq, &data[2], 2);
    header.sequenceNumber = ntohs(seq);

    // Parse timestamp (32 bits)
    uint32_t ts;
    std::memcpy(&ts, &data[4], 4);
    header.timestamp = ntohl(ts);

    // Parse SSRC (32 bits)
    uint32_t ssrc;
    std::memcpy(&ssrc, &data[8], 4);
    header.ssrc = ntohl(ssrc);

    // Parse CSRC list
    size_t header_size = 12 + header.csrcCount * 4;
    if (size < header_size) {
        throw RtpError(RtpErrorCode::InvalidPacket, "Packet too small for CSRC list");
    }

    header.csrc.resize(header.csrcCount);
    for (int i = 0; i < header.csrcCount; ++i) {
        uint32_t csrc;
        std::memcpy(&csrc, &data[12 + i * 4], 4);
        header.csrc[i] = ntohl(csrc);
    }

    // Create packet
    RtpPacket packet(header);

    // Parse payload
    if (size > header_size) {
        packet.setPayload(&data[header_size], size - header_size);
    }

    return packet;
}

// RTCP SenderReport implementation
RtcpSenderReport::RtcpSenderReport() {
    // Initialize with default values
}

std::vector<uint8_t> RtcpSenderReport::serialize() const {
    // RTCP SR packet has a fixed size of 28 bytes
    std::vector<uint8_t> buffer(28);

    // RTCP header (8 bytes)
    // V=2, P=0, RC=0, PT=SR(200)
    buffer[0] = (2 << 6) | 0;
    buffer[1] = 200;

    // Length field (32 bits) - length of the packet in 32-bit words minus 1
    uint16_t length = 6; // (28 bytes / 4) - 1 = 6
    uint16_t length_n = htons(length);
    std::memcpy(&buffer[2], &length_n, 2);

    // SSRC (32 bits)
    uint32_t ssrc = htonl(ssrc_);
    std::memcpy(&buffer[4], &ssrc, 4);

    // NTP timestamp (64 bits)
    uint32_t ntp_high = htonl(ntp_timestamp_high_);
    std::memcpy(&buffer[8], &ntp_high, 4);
    uint32_t ntp_low = htonl(ntp_timestamp_low_);
    std::memcpy(&buffer[12], &ntp_low, 4);

    // RTP timestamp (32 bits)
    uint32_t rtp_ts = htonl(rtp_timestamp_);
    std::memcpy(&buffer[16], &rtp_ts, 4);

    // Packet count (32 bits)
    uint32_t pkt_count = htonl(packet_count_);
    std::memcpy(&buffer[20], &pkt_count, 4);

    // Octet count (32 bits)
    uint32_t oct_count = htonl(octet_count_);
    std::memcpy(&buffer[24], &oct_count, 4);

    return buffer;
}

// RTCP ReceiverReport implementation
RtcpReceiverReport::RtcpReceiverReport() {
    // Initialize with default values
}

std::vector<uint8_t> RtcpReceiverReport::serialize() const {
    // Calculate size:
    // RTCP header (8 bytes) + report blocks (24 bytes each)
    size_t size = 8 + report_blocks_.size() * 24;
    std::vector<uint8_t> buffer(size);

    // RTCP header (8 bytes)
    // V=2, P=0, RC=number of reception report blocks
    buffer[0] = (2 << 6) | (report_blocks_.size() & 0x1F);
    buffer[1] = 201; // PT=RR(201)

    // Length field (32 bits) - length of the packet in 32-bit words minus 1
    uint16_t length = (size / 4) - 1;
    uint16_t length_n = htons(length);
    std::memcpy(&buffer[2], &length_n, 2);

    // SSRC (32 bits)
    uint32_t ssrc = htonl(ssrc_);
    std::memcpy(&buffer[4], &ssrc, 4);

    // Report blocks
    for (size_t i = 0; i < report_blocks_.size(); ++i) {
        const auto& block = report_blocks_[i];
        size_t offset = 8 + i * 24;

        // SSRC (32 bits)
        uint32_t block_ssrc = htonl(block.ssrc);
        std::memcpy(&buffer[offset], &block_ssrc, 4);

        // Fraction lost (8 bits) and cumulative lost (24 bits)
        buffer[offset + 4] = block.fractionLost;
        buffer[offset + 5] = (block.cumulativeLost >> 16) & 0xFF;
        buffer[offset + 6] = (block.cumulativeLost >> 8) & 0xFF;
        buffer[offset + 7] = block.cumulativeLost & 0xFF;

        // Extended highest sequence number (32 bits)
        uint32_t seq = htonl(block.extendedHighestSeqNum);
        std::memcpy(&buffer[offset + 8], &seq, 4);

        // Interarrival jitter (32 bits)
        uint32_t jitter = htonl(block.interarrivalJitter);
        std::memcpy(&buffer[offset + 12], &jitter, 4);

        // Last SR (32 bits)
        uint32_t last_sr = htonl(block.lastSR);
        std::memcpy(&buffer[offset + 16], &last_sr, 4);

        // Delay since last SR (32 bits)
        uint32_t delay = htonl(block.delaySinceLastSR);
        std::memcpy(&buffer[offset + 20], &delay, 4);
    }

    return buffer;
}

// RTCP packet deserialization
std::unique_ptr<RtcpPacket> RtcpPacket::deserialize(const uint8_t* data, size_t size) {
    // Verify minimum size (RTCP header is 8 bytes)
    if (size < 8) {
        throw RtpError(RtpErrorCode::InvalidPacket, "RTCP packet too small");
    }

    // Check version
    int version = (data[0] >> 6) & 0x03;
    if (version != 2) {
        throw RtpError(RtpErrorCode::InvalidPacket, "Invalid RTCP version");
    }

    // Get packet type
    uint8_t packet_type = data[1];

    // Check packet type and create appropriate packet
    switch (packet_type) {
        case static_cast<uint8_t>(RtcpPacketType::SR): {
            // Sender report
            if (size < 28) {
                throw RtpError(RtpErrorCode::InvalidPacket, "RTCP SR packet too small");
            }

            auto sr = std::make_unique<RtcpSenderReport>();

            // Parse SSRC
            uint32_t ssrc;
            std::memcpy(&ssrc, &data[4], 4);
            sr->setSsrc(ntohl(ssrc));

            // Parse NTP timestamp
            uint32_t ntp_high, ntp_low;
            std::memcpy(&ntp_high, &data[8], 4);
            std::memcpy(&ntp_low, &data[12], 4);
            sr->setNtpTimestamp(ntohl(ntp_high), ntohl(ntp_low));

            // Parse RTP timestamp
            uint32_t rtp_ts;
            std::memcpy(&rtp_ts, &data[16], 4);
            sr->setRtpTimestamp(ntohl(rtp_ts));

            // Parse packet count
            uint32_t pkt_count;
            std::memcpy(&pkt_count, &data[20], 4);
            sr->setPacketCount(ntohl(pkt_count));

            // Parse octet count
            uint32_t oct_count;
            std::memcpy(&oct_count, &data[24], 4);
            sr->setOctetCount(ntohl(oct_count));

            return sr;
        }

        case static_cast<uint8_t>(RtcpPacketType::RR): {
            // Receiver report
            auto rr = std::make_unique<RtcpReceiverReport>();

            // Parse SSRC
            uint32_t ssrc;
            std::memcpy(&ssrc, &data[4], 4);
            rr->setSsrc(ntohl(ssrc));

            // Get reception report count
            uint8_t rc = data[0] & 0x1F;

            // Ensure packet is large enough for all report blocks
            if (size < 8 + rc * 24) {
                throw RtpError(RtpErrorCode::InvalidPacket, "RTCP RR packet too small for report blocks");
            }

            // Parse report blocks
            for (int i = 0; i < rc; ++i) {
                RtcpReceiverReport::ReportBlock block;
                size_t offset = 8 + i * 24;

                // Parse SSRC
                uint32_t block_ssrc;
                std::memcpy(&block_ssrc, &data[offset], 4);
                block.ssrc = ntohl(block_ssrc);

                // Parse fraction lost and cumulative lost
                block.fractionLost = data[offset + 4];
                block.cumulativeLost = (data[offset + 5] << 16) |
                                      (data[offset + 6] << 8) |
                                       data[offset + 7];

                // Parse extended highest sequence number
                uint32_t seq;
                std::memcpy(&seq, &data[offset + 8], 4);
                block.extendedHighestSeqNum = ntohl(seq);

                // Parse interarrival jitter
                uint32_t jitter;
                std::memcpy(&jitter, &data[offset + 12], 4);
                block.interarrivalJitter = ntohl(jitter);

                // Parse last SR
                uint32_t last_sr;
                std::memcpy(&last_sr, &data[offset + 16], 4);
                block.lastSR = ntohl(last_sr);

                // Parse delay since last SR
                uint32_t delay;
                std::memcpy(&delay, &data[offset + 20], 4);
                block.delaySinceLastSR = ntohl(delay);

                rr->addReportBlock(block);
            }

            return rr;
        }

        default:
            // Unsupported RTCP packet type
            throw RtpError(RtpErrorCode::NotImplemented,
                "Unsupported RTCP packet type: " + std::to_string(packet_type));
    }
}

} // namespace fmus::rtp