#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include <chrono>
#include <functional>

#include <fmus/core/error.hpp>
#include <fmus/core/task.hpp>
#include <fmus/media/media.hpp>
#include <fmus/network/network.hpp>

namespace fmus::rtp {

// Forward declarations
class RtpPacket;
class RtcpPacket;
class RtpSession;
class RtpStream;
class RtpSender;
class RtpReceiver;

// RTP error codes
enum class RtpErrorCode {
    Success = 0,
    InvalidPacket,
    PacketLost,
    StreamNotFound,
    InvalidParameter,
    SessionClosed,
    NetworkError,
    NotImplemented,
    UnknownError
};

// RTP error exception
class RtpError : public core::Error {
public:
    explicit RtpError(RtpErrorCode code, const std::string& message);
    RtpErrorCode rtpCode() const noexcept { return code_; }

private:
    RtpErrorCode code_;
};

// RTP payload types (standard ones from RFC 3551)
enum class RtpPayloadType : uint8_t {
    PCMU = 0,      // G.711 mu-law
    GSM = 3,       // GSM
    G723 = 4,      // G.723
    DVI4_8K = 5,   // DVI4 8KHz
    DVI4_16K = 6,  // DVI4 16KHz
    LPC = 7,       // LPC
    PCMA = 8,      // G.711 a-law
    G722 = 9,      // G.722
    L16_STEREO = 10, // L16 stereo
    L16_MONO = 11,   // L16 mono
    QCELP = 12,      // QCELP
    CN = 13,         // Comfort noise
    MPA = 14,        // MPEG audio
    G728 = 15,       // G.728
    DVI4_11K = 16,   // DVI4 11KHz
    DVI4_22K = 17,   // DVI4 22KHz
    G729 = 18,       // G.729
    H263 = 34,       // H.263
    // Dynamic payload types (96-127)
    DYNAMIC_FIRST = 96,
    H264 = 96,       // H.264
    VP8 = 97,        // VP8
    VP9 = 98,        // VP9
    OPUS = 99,       // Opus
    DYNAMIC_LAST = 127
};

// RTP packet header structure
struct RtpHeader {
    bool version : 2;       // Version (2 bits)
    bool padding : 1;       // Padding flag (1 bit)
    bool extension : 1;     // Extension flag (1 bit)
    uint8_t csrcCount : 4;  // CSRC count (4 bits)
    bool marker : 1;        // Marker bit (1 bit)
    uint8_t payloadType : 7; // Payload type (7 bits)
    uint16_t sequenceNumber; // Sequence number (16 bits)
    uint32_t timestamp;      // Timestamp (32 bits)
    uint32_t ssrc;           // SSRC identifier (32 bits)
    std::vector<uint32_t> csrc; // CSRC list (optional)
};

// RTP packet class
class RtpPacket {
public:
    // Constructor
    RtpPacket();
    explicit RtpPacket(const RtpHeader& header);
    RtpPacket(const RtpHeader& header, const void* payload, size_t payload_size);

    // Serialize/deserialize
    std::vector<uint8_t> serialize() const;
    static RtpPacket deserialize(const uint8_t* data, size_t size);

    // Getters/setters
    const RtpHeader& header() const { return header_; }
    RtpHeader& header() { return header_; }
    const uint8_t* payload() const { return payload_.data(); }
    uint8_t* payload() { return payload_.data(); }
    size_t payloadSize() const { return payload_.size(); }
    void setPayload(const void* data, size_t size);

    // Utility methods
    RtpPayloadType payloadType() const {
        return static_cast<RtpPayloadType>(header_.payloadType);
    }
    void setPayloadType(RtpPayloadType type) {
        header_.payloadType = static_cast<uint8_t>(type);
    }

private:
    RtpHeader header_;
    std::vector<uint8_t> payload_;
};

// RTCP packet types
enum class RtcpPacketType : uint8_t {
    SR = 200,    // Sender Report
    RR = 201,    // Receiver Report
    SDES = 202,  // Source Description
    BYE = 203,   // Goodbye
    APP = 204    // Application-defined
};

// RTCP packet base class
class RtcpPacket {
public:
    virtual ~RtcpPacket() = default;

    // Packet type
    virtual RtcpPacketType type() const = 0;

    // Serialize/deserialize
    virtual std::vector<uint8_t> serialize() const = 0;
    static std::unique_ptr<RtcpPacket> deserialize(const uint8_t* data, size_t size);
};

// RTCP sender report
class RtcpSenderReport : public RtcpPacket {
public:
    RtcpSenderReport();

    // Implement RtcpPacket interface
    RtcpPacketType type() const override { return RtcpPacketType::SR; }
    std::vector<uint8_t> serialize() const override;

    // Getters/setters
    uint32_t ssrc() const { return ssrc_; }
    void setSsrc(uint32_t ssrc) { ssrc_ = ssrc; }
    uint32_t ntpTimestampHigh() const { return ntp_timestamp_high_; }
    uint32_t ntpTimestampLow() const { return ntp_timestamp_low_; }
    void setNtpTimestamp(uint32_t high, uint32_t low) {
        ntp_timestamp_high_ = high;
        ntp_timestamp_low_ = low;
    }
    uint32_t rtpTimestamp() const { return rtp_timestamp_; }
    void setRtpTimestamp(uint32_t timestamp) { rtp_timestamp_ = timestamp; }
    uint32_t packetCount() const { return packet_count_; }
    void setPacketCount(uint32_t count) { packet_count_ = count; }
    uint32_t octetCount() const { return octet_count_; }
    void setOctetCount(uint32_t count) { octet_count_ = count; }

private:
    uint32_t ssrc_ = 0;
    uint32_t ntp_timestamp_high_ = 0;
    uint32_t ntp_timestamp_low_ = 0;
    uint32_t rtp_timestamp_ = 0;
    uint32_t packet_count_ = 0;
    uint32_t octet_count_ = 0;
};

// RTCP receiver report
class RtcpReceiverReport : public RtcpPacket {
public:
    // Reception report block
    struct ReportBlock {
        uint32_t ssrc;
        uint8_t fractionLost;
        uint32_t cumulativeLost : 24;
        uint32_t extendedHighestSeqNum;
        uint32_t interarrivalJitter;
        uint32_t lastSR;
        uint32_t delaySinceLastSR;
    };

    RtcpReceiverReport();

    // Implement RtcpPacket interface
    RtcpPacketType type() const override { return RtcpPacketType::RR; }
    std::vector<uint8_t> serialize() const override;

    // Getters/setters
    uint32_t ssrc() const { return ssrc_; }
    void setSsrc(uint32_t ssrc) { ssrc_ = ssrc; }
    const std::vector<ReportBlock>& reportBlocks() const { return report_blocks_; }
    void addReportBlock(const ReportBlock& block) { report_blocks_.push_back(block); }

private:
    uint32_t ssrc_ = 0;
    std::vector<ReportBlock> report_blocks_;
};

// RTP media-specific parameters
struct RtpAudioParams {
    RtpPayloadType payload_type = RtpPayloadType::PCMU;
    uint32_t clock_rate = 8000;
    uint8_t channels = 1;
    uint16_t max_packet_size = 1400;
};

struct RtpVideoParams {
    RtpPayloadType payload_type = RtpPayloadType::H264;
    uint32_t clock_rate = 90000;
    uint16_t max_packet_size = 1400;
};

// RTP stream interface
class RtpStream {
public:
    virtual ~RtpStream() = default;

    // Stream info
    virtual uint32_t ssrc() const = 0;
    virtual media::MediaType mediaType() const = 0;

    // Statistics
    virtual uint32_t packetsSent() const = 0;
    virtual uint32_t packetsReceived() const = 0;
    virtual uint32_t packetsLost() const = 0;
    virtual double fractionLost() const = 0;
    virtual uint32_t bytesSent() const = 0;
    virtual uint32_t bytesReceived() const = 0;
    virtual double jitter() const = 0;

    // RTCP functions
    virtual core::Task<void> sendRtcpPacket(const RtcpPacket& packet) = 0;

    // Events
    core::EventEmitter<const RtcpPacket&> onRtcpPacket;
};

// RTP audio stream
class RtpAudioStream : public RtpStream {
public:
    // Implementation of RtpStream interface
    media::MediaType mediaType() const override { return media::MediaType::Audio; }

    // Audio-specific methods
    virtual RtpAudioParams params() const = 0;

    // Events
    core::EventEmitter<const media::AudioFrame&> onFrame;
};

// RTP video stream
class RtpVideoStream : public RtpStream {
public:
    // Implementation of RtpStream interface
    media::MediaType mediaType() const override { return media::MediaType::Video; }

    // Video-specific methods
    virtual RtpVideoParams params() const = 0;

    // Events
    core::EventEmitter<const media::VideoFrame&> onFrame;
};

// RTP session interface
class RtpSession {
public:
    virtual ~RtpSession() = default;

    // Session control
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;

    // Stream management
    virtual std::vector<std::shared_ptr<RtpStream>> getStreams() const = 0;
    virtual std::shared_ptr<RtpStream> getStreamBySsrc(uint32_t ssrc) = 0;

    // Create session
    static std::unique_ptr<RtpSession> create(const network::NetworkAddress& local_addr);
};

// RTP sender interface
class RtpSender {
public:
    virtual ~RtpSender() = default;

    // Send frame
    virtual core::Task<void> sendAudioFrame(const media::AudioFrame& frame) = 0;
    virtual core::Task<void> sendVideoFrame(const media::VideoFrame& frame) = 0;

    // Sender stream
    virtual std::shared_ptr<RtpStream> stream() const = 0;

    // Create senders
    static std::unique_ptr<RtpSender> createAudioSender(
        std::shared_ptr<RtpSession> session,
        const network::NetworkAddress& remote_addr,
        const RtpAudioParams& params);

    static std::unique_ptr<RtpSender> createVideoSender(
        std::shared_ptr<RtpSession> session,
        const network::NetworkAddress& remote_addr,
        const RtpVideoParams& params);
};

// RTP receiver interface
class RtpReceiver {
public:
    virtual ~RtpReceiver() = default;

    // Receiver stream
    virtual std::shared_ptr<RtpStream> stream() const = 0;

    // Frame output
    virtual std::shared_ptr<media::MediaTrack> track() const = 0;

    // Create receivers
    static std::unique_ptr<RtpReceiver> createAudioReceiver(
        std::shared_ptr<RtpSession> session,
        const RtpAudioParams& params);

    static std::unique_ptr<RtpReceiver> createVideoReceiver(
        std::shared_ptr<RtpSession> session,
        const RtpVideoParams& params);
};

} // namespace fmus::rtp