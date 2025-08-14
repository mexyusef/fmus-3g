#include "fmus/network/transport.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>

namespace fmus::network {

// SipTransport implementation
SipTransport::SipTransport() {
}

SipTransport::~SipTransport() {
    stop();
}

bool SipTransport::startUdp(const SocketAddress& bind_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (udp_socket_) {
        core::Logger::warn("UDP transport already started");
        return true;
    }
    
    udp_socket_ = createUdpSocket();
    
    udp_socket_->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
        onUdpData(data, from);
    });
    
    udp_socket_->setErrorCallback([this](const std::string& error) {
        onError("UDP: " + error);
    });
    
    if (!udp_socket_->bind(bind_address)) {
        udp_socket_.reset();
        return false;
    }
    
    udp_socket_->startReceiving();
    core::Logger::info("SIP UDP transport started on {}", bind_address.toString());
    return true;
}

bool SipTransport::startTcp(const SocketAddress& bind_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (tcp_server_) {
        core::Logger::warn("TCP transport already started");
        return true;
    }
    
    tcp_server_ = createTcpSocket();
    
    tcp_server_->setConnectionCallback([this](std::shared_ptr<Socket> connection) {
        onTcpConnection(connection);
    });
    
    tcp_server_->setErrorCallback([this](const std::string& error) {
        onError("TCP Server: " + error);
    });
    
    if (!tcp_server_->bind(bind_address)) {
        tcp_server_.reset();
        return false;
    }
    
    if (!tcp_server_->listen()) {
        tcp_server_.reset();
        return false;
    }
    
    tcp_server_->acceptConnections();
    core::Logger::info("SIP TCP transport started on {}", bind_address.toString());
    return true;
}

void SipTransport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (udp_socket_) {
        udp_socket_->close();
        udp_socket_.reset();
    }
    
    if (tcp_server_) {
        tcp_server_->close();
        tcp_server_.reset();
    }
    
    for (auto& [addr, connection] : tcp_connections_) {
        connection->close();
    }
    tcp_connections_.clear();
    
    core::Logger::info("SIP transport stopped");
}

bool SipTransport::sendMessage(const fmus::sip::SipMessage& message, const SocketAddress& destination) {
    return sendMessage(message.toString(), destination);
}

bool SipTransport::sendMessage(const std::string& raw_message, const SocketAddress& destination) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Try UDP first if available
    if (udp_socket_) {
        std::vector<uint8_t> data(raw_message.begin(), raw_message.end());
        if (udp_socket_->send(data, destination)) {
            stats_.messages_sent++;
            stats_.bytes_sent += data.size();
            return true;
        }
    }
    
    // Fall back to TCP
    auto connection = getTcpConnection(destination);
    if (connection) {
        std::vector<uint8_t> data(raw_message.begin(), raw_message.end());
        if (connection->send(data)) {
            stats_.messages_sent++;
            stats_.bytes_sent += data.size();
            return true;
        }
    }
    
    stats_.errors++;
    return false;
}

std::shared_ptr<TcpSocket> SipTransport::getTcpConnection(const SocketAddress& address) {
    std::string key = address.toString();
    
    auto it = tcp_connections_.find(key);
    if (it != tcp_connections_.end()) {
        return it->second;
    }
    
    // Create new connection
    auto connection = createTcpSocket();
    
    connection->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
        onTcpData(data, from);
    });
    
    connection->setErrorCallback([this, key](const std::string& error) {
        onError("TCP Connection " + key + ": " + error);
        closeTcpConnection(SocketAddress{}); // Will be handled by key lookup
    });
    
    if (connection->connect(address)) {
        connection->startReceiving();
        tcp_connections_[key] = connection;
        return connection;
    }
    
    return nullptr;
}

void SipTransport::closeTcpConnection(const SocketAddress& address) {
    std::string key = address.toString();
    auto it = tcp_connections_.find(key);
    if (it != tcp_connections_.end()) {
        it->second->close();
        tcp_connections_.erase(it);
    }
}

void SipTransport::onUdpData(const std::vector<uint8_t>& data, const SocketAddress& from) {
    std::string message(data.begin(), data.end());
    processMessage(message, from);
    stats_.messages_received++;
    stats_.bytes_received += data.size();
}

void SipTransport::onTcpData(const std::vector<uint8_t>& data, const SocketAddress& from) {
    std::string message(data.begin(), data.end());
    processMessage(message, from);
    stats_.messages_received++;
    stats_.bytes_received += data.size();
}

void SipTransport::onTcpConnection(std::shared_ptr<Socket> connection) {
    auto tcp_conn = std::dynamic_pointer_cast<TcpSocket>(connection);
    if (!tcp_conn) return;
    
    std::string key = tcp_conn->getRemoteAddress().toString();
    
    tcp_conn->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
        onTcpData(data, from);
    });
    
    tcp_conn->setErrorCallback([this, key](const std::string& error) {
        onError("TCP Connection " + key + ": " + error);
    });
    
    tcp_conn->startReceiving();
    
    std::lock_guard<std::mutex> lock(mutex_);
    tcp_connections_[key] = tcp_conn;
    
    core::Logger::info("New TCP connection established: {}", key);
}

void SipTransport::onError(const std::string& error) {
    stats_.errors++;
    if (error_callback_) {
        error_callback_(error);
    }
}

void SipTransport::processMessage(const std::string& message, const SocketAddress& from) {
    try {
        if (message_callback_) {
            fmus::sip::SipMessage sip_message = fmus::sip::SipMessage::fromString(message);
            message_callback_(sip_message, from);
        }
    } catch (const std::exception& e) {
        onError("Failed to parse SIP message from " + from.toString() + ": " + e.what());
    }
}

// RtpTransport implementation
RtpTransport::RtpTransport() {
}

RtpTransport::~RtpTransport() {
    stop();
}

bool RtpTransport::start(const SocketAddress& rtp_address, const SocketAddress& rtcp_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (rtp_socket_) {
        core::Logger::warn("RTP transport already started");
        return true;
    }
    
    // Start RTP socket
    rtp_socket_ = createUdpSocket();
    
    rtp_socket_->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
        onRtpData(data, from);
    });
    
    rtp_socket_->setErrorCallback([this](const std::string& error) {
        onError("RTP: " + error);
    });
    
    if (!rtp_socket_->bind(rtp_address)) {
        rtp_socket_.reset();
        return false;
    }
    
    rtp_socket_->startReceiving();
    
    // Start RTCP socket if address provided
    if (rtcp_address.port != 0) {
        rtcp_socket_ = createUdpSocket();
        
        rtcp_socket_->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
            onRtcpData(data, from);
        });
        
        rtcp_socket_->setErrorCallback([this](const std::string& error) {
            onError("RTCP: " + error);
        });
        
        if (!rtcp_socket_->bind(rtcp_address)) {
            rtp_socket_->close();
            rtp_socket_.reset();
            rtcp_socket_.reset();
            return false;
        }
        
        rtcp_socket_->startReceiving();
        core::Logger::info("RTP transport started on {} (RTCP: {})", 
                          rtp_address.toString(), rtcp_address.toString());
    } else {
        core::Logger::info("RTP transport started on {}", rtp_address.toString());
    }
    
    return true;
}

void RtpTransport::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (rtp_socket_) {
        rtp_socket_->close();
        rtp_socket_.reset();
    }
    
    if (rtcp_socket_) {
        rtcp_socket_->close();
        rtcp_socket_.reset();
    }
    
    core::Logger::info("RTP transport stopped");
}

bool RtpTransport::sendRtpPacket(const fmus::rtp::RtpPacket& packet, const SocketAddress& destination) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!rtp_socket_) {
        onError("RTP socket not initialized");
        return false;
    }

    auto data = packet.serialize();
    if (rtp_socket_->send(data, destination)) {
        stats_.rtp_packets_sent++;
        stats_.bytes_sent += data.size();
        return true;
    }

    stats_.errors++;
    return false;
}

bool RtpTransport::sendRtcpPacket(const fmus::rtp::RtcpPacket& packet, const SocketAddress& destination) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!rtcp_socket_) {
        onError("RTCP socket not initialized");
        return false;
    }

    auto data = packet.serialize();
    if (rtcp_socket_->send(data, destination)) {
        stats_.rtcp_packets_sent++;
        stats_.bytes_sent += data.size();
        return true;
    }

    stats_.errors++;
    return false;
}

void RtpTransport::onRtpData(const std::vector<uint8_t>& data, const SocketAddress& from) {
    try {
        auto packet = fmus::rtp::RtpPacket::deserialize(data.data(), data.size());
        if (packet && rtp_callback_) {
            rtp_callback_(*packet, from);
            stats_.rtp_packets_received++;
            stats_.bytes_received += data.size();
        }
    } catch (const std::exception& e) {
        onError("Failed to parse RTP packet from " + from.toString() + ": " + e.what());
    }
}

void RtpTransport::onRtcpData(const std::vector<uint8_t>& data, const SocketAddress& from) {
    try {
        auto packet = fmus::rtp::RtcpPacket::deserialize(data.data(), data.size());
        if (packet && rtcp_callback_) {
            rtcp_callback_(*packet, from);
            stats_.rtcp_packets_received++;
            stats_.bytes_received += data.size();
        }
    } catch (const std::exception& e) {
        onError("Failed to parse RTCP packet from " + from.toString() + ": " + e.what());
    }
}

void RtpTransport::onError(const std::string& error) {
    stats_.errors++;
    if (error_callback_) {
        error_callback_(error);
    }
}

// TransportManager implementation
TransportManager::TransportManager() {
}

TransportManager::~TransportManager() {
    shutdown();
}

bool TransportManager::initialize(const Config& config) {
    config_ = config;

    bool success = true;

    if (config_.enable_sip_udp) {
        if (!sip_transport_.startUdp(config_.sip_udp_address)) {
            core::Logger::error("Failed to start SIP UDP transport");
            success = false;
        }
    }

    if (config_.enable_sip_tcp) {
        if (!sip_transport_.startTcp(config_.sip_tcp_address)) {
            core::Logger::error("Failed to start SIP TCP transport");
            success = false;
        }
    }

    if (config_.enable_rtp) {
        if (!rtp_transport_.start(config_.rtp_address, config_.rtcp_address)) {
            core::Logger::error("Failed to start RTP transport");
            success = false;
        }
    }

    initialized_ = success;
    if (success) {
        core::Logger::info("Transport manager initialized successfully");
    }

    return success;
}

void TransportManager::shutdown() {
    if (initialized_) {
        sip_transport_.stop();
        rtp_transport_.stop();
        initialized_ = false;
        core::Logger::info("Transport manager shut down");
    }
}

} // namespace fmus::network
