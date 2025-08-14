#pragma once

#include "socket.hpp"
#include "fmus/sip/message.hpp"
#include "fmus/rtp/packet.hpp"
#include <unordered_map>
#include <queue>

namespace fmus::network {

class SipTransport {
public:
    using MessageCallback = std::function<void(const fmus::sip::SipMessage&, const SocketAddress&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    SipTransport();
    ~SipTransport();
    
    // Transport management
    bool startUdp(const SocketAddress& bind_address);
    bool startTcp(const SocketAddress& bind_address);
    void stop();
    
    // Message sending
    bool sendMessage(const fmus::sip::SipMessage& message, const SocketAddress& destination);
    bool sendMessage(const std::string& raw_message, const SocketAddress& destination);
    
    // Callbacks
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Connection management
    std::shared_ptr<TcpSocket> getTcpConnection(const SocketAddress& address);
    void closeTcpConnection(const SocketAddress& address);
    
    // Statistics
    struct Stats {
        uint64_t messages_sent = 0;
        uint64_t messages_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t errors = 0;
    };
    
    Stats getStats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    void onUdpData(const std::vector<uint8_t>& data, const SocketAddress& from);
    void onTcpData(const std::vector<uint8_t>& data, const SocketAddress& from);
    void onTcpConnection(std::shared_ptr<Socket> connection);
    void onError(const std::string& error);
    
    void processMessage(const std::string& message, const SocketAddress& from);
    
    std::shared_ptr<UdpSocket> udp_socket_;
    std::shared_ptr<TcpSocket> tcp_server_;
    std::unordered_map<std::string, std::shared_ptr<TcpSocket>> tcp_connections_;
    
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    
    mutable std::mutex mutex_;
    Stats stats_;
};

class RtpTransport {
public:
    using PacketCallback = std::function<void(const fmus::rtp::RtpPacket&, const SocketAddress&)>;
    using RtcpCallback = std::function<void(const fmus::rtp::RtcpPacket&, const SocketAddress&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    RtpTransport();
    ~RtpTransport();
    
    // Transport management
    bool start(const SocketAddress& rtp_address, const SocketAddress& rtcp_address = {});
    void stop();
    
    // Packet sending
    bool sendRtpPacket(const fmus::rtp::RtpPacket& packet, const SocketAddress& destination);
    bool sendRtcpPacket(const fmus::rtp::RtcpPacket& packet, const SocketAddress& destination);
    
    // Callbacks
    void setRtpCallback(PacketCallback callback) { rtp_callback_ = callback; }
    void setRtcpCallback(RtcpCallback callback) { rtcp_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Statistics
    struct Stats {
        uint64_t rtp_packets_sent = 0;
        uint64_t rtp_packets_received = 0;
        uint64_t rtcp_packets_sent = 0;
        uint64_t rtcp_packets_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t errors = 0;
    };
    
    Stats getStats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    void onRtpData(const std::vector<uint8_t>& data, const SocketAddress& from);
    void onRtcpData(const std::vector<uint8_t>& data, const SocketAddress& from);
    void onError(const std::string& error);
    
    std::shared_ptr<UdpSocket> rtp_socket_;
    std::shared_ptr<UdpSocket> rtcp_socket_;
    
    PacketCallback rtp_callback_;
    RtcpCallback rtcp_callback_;
    ErrorCallback error_callback_;
    
    mutable std::mutex mutex_;
    Stats stats_;
};

// Transport Manager - coordinates all network transports
class TransportManager {
public:
    TransportManager();
    ~TransportManager();
    
    // SIP Transport
    SipTransport& getSipTransport() { return sip_transport_; }
    
    // RTP Transport
    RtpTransport& getRtpTransport() { return rtp_transport_; }
    
    // Global operations
    void shutdown();
    
    // Configuration
    struct Config {
        SocketAddress sip_udp_address{"0.0.0.0", 5060};
        SocketAddress sip_tcp_address{"0.0.0.0", 5060};
        SocketAddress rtp_address{"0.0.0.0", 0}; // 0 = auto-assign
        SocketAddress rtcp_address{"0.0.0.0", 0}; // 0 = auto-assign
        bool enable_sip_udp = true;
        bool enable_sip_tcp = true;
        bool enable_rtp = true;
    };
    
    bool initialize(const Config& config);
    
private:
    SipTransport sip_transport_;
    RtpTransport rtp_transport_;
    Config config_;
    bool initialized_ = false;
};

} // namespace fmus::network
