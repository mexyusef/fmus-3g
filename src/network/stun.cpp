#include "fmus/network/stun.hpp"
#include "fmus/core/logger.hpp"
#include <random>
#include <cstring>
#include <sstream>
#include <arpa/inet.h>

namespace fmus::network {

// STUN Magic Cookie (RFC 5389)
static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;

// StunAttribute implementation
uint32_t StunAttribute::asUint32() const {
    if (value.size() >= 4) {
        return ntohl(*reinterpret_cast<const uint32_t*>(value.data()));
    }
    return 0;
}

uint16_t StunAttribute::asUint16() const {
    if (value.size() >= 2) {
        return ntohs(*reinterpret_cast<const uint16_t*>(value.data()));
    }
    return 0;
}

SocketAddress StunAttribute::asAddress() const {
    if (value.size() >= 8) {
        uint16_t family = ntohs(*reinterpret_cast<const uint16_t*>(value.data() + 1));
        uint16_t port = ntohs(*reinterpret_cast<const uint16_t*>(value.data() + 2));
        
        if (family == 0x01) { // IPv4
            uint32_t ip = *reinterpret_cast<const uint32_t*>(value.data() + 4);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ip, ip_str, INET_ADDRSTRLEN);
            return SocketAddress(ip_str, port);
        }
    }
    return SocketAddress();
}

SocketAddress StunAttribute::asXorAddress(const std::vector<uint8_t>& transaction_id) const {
    if (value.size() >= 8) {
        uint16_t family = ntohs(*reinterpret_cast<const uint16_t*>(value.data() + 1));
        uint16_t xor_port = ntohs(*reinterpret_cast<const uint16_t*>(value.data() + 2));
        
        if (family == 0x01) { // IPv4
            uint32_t xor_ip = *reinterpret_cast<const uint32_t*>(value.data() + 4);
            
            // XOR with magic cookie for port
            uint16_t port = xor_port ^ (STUN_MAGIC_COOKIE >> 16);
            
            // XOR with magic cookie for IP
            uint32_t ip = xor_ip ^ STUN_MAGIC_COOKIE;
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ip, ip_str, INET_ADDRSTRLEN);
            return SocketAddress(ip_str, port);
        }
    }
    return SocketAddress();
}

// StunMessage implementation
StunMessage::StunMessage(StunMessageType type) : type_(type) {
    transaction_id_ = generateTransactionId();
}

StunMessage::StunMessage(const uint8_t* data, size_t size) {
    parse(data, size);
}

void StunMessage::addAttribute(const StunAttribute& attr) {
    attributes_.push_back(attr);
}

void StunMessage::addAttribute(StunAttributeType type, const std::vector<uint8_t>& value) {
    attributes_.emplace_back(type, value);
}

void StunMessage::addAttribute(StunAttributeType type, const std::string& value) {
    attributes_.emplace_back(type, value);
}

void StunMessage::addAttribute(StunAttributeType type, uint32_t value) {
    std::vector<uint8_t> data(4);
    *reinterpret_cast<uint32_t*>(data.data()) = htonl(value);
    attributes_.emplace_back(type, data);
}

void StunMessage::addAttribute(StunAttributeType type, uint16_t value) {
    std::vector<uint8_t> data(2);
    *reinterpret_cast<uint16_t*>(data.data()) = htons(value);
    attributes_.emplace_back(type, data);
}

void StunMessage::addAddressAttribute(StunAttributeType type, const SocketAddress& address) {
    std::vector<uint8_t> data(8);
    data[0] = 0x00; // Reserved
    data[1] = 0x01; // IPv4
    *reinterpret_cast<uint16_t*>(data.data() + 2) = htons(address.port);
    
    struct sockaddr_in addr = address.toSockAddr();
    *reinterpret_cast<uint32_t*>(data.data() + 4) = addr.sin_addr.s_addr;
    
    attributes_.emplace_back(type, data);
}

void StunMessage::addXorAddressAttribute(StunAttributeType type, const SocketAddress& address) {
    std::vector<uint8_t> data(8);
    data[0] = 0x00; // Reserved
    data[1] = 0x01; // IPv4
    
    // XOR port with magic cookie
    uint16_t xor_port = address.port ^ (STUN_MAGIC_COOKIE >> 16);
    *reinterpret_cast<uint16_t*>(data.data() + 2) = htons(xor_port);
    
    // XOR IP with magic cookie
    struct sockaddr_in addr = address.toSockAddr();
    uint32_t xor_ip = addr.sin_addr.s_addr ^ STUN_MAGIC_COOKIE;
    *reinterpret_cast<uint32_t*>(data.data() + 4) = xor_ip;
    
    attributes_.emplace_back(type, data);
}

bool StunMessage::hasAttribute(StunAttributeType type) const {
    for (const auto& attr : attributes_) {
        if (attr.type == type) {
            return true;
        }
    }
    return false;
}

StunAttribute StunMessage::getAttribute(StunAttributeType type) const {
    for (const auto& attr : attributes_) {
        if (attr.type == type) {
            return attr;
        }
    }
    return StunAttribute(type, std::vector<uint8_t>());
}

std::vector<StunAttribute> StunMessage::getAttributes(StunAttributeType type) const {
    std::vector<StunAttribute> result;
    for (const auto& attr : attributes_) {
        if (attr.type == type) {
            result.push_back(attr);
        }
    }
    return result;
}

void StunMessage::removeAttribute(StunAttributeType type) {
    attributes_.erase(
        std::remove_if(attributes_.begin(), attributes_.end(),
                      [type](const StunAttribute& attr) { return attr.type == type; }),
        attributes_.end());
}

std::vector<uint8_t> StunMessage::serialize() const {
    std::vector<uint8_t> result;
    
    // Calculate total attribute length
    size_t attr_length = 0;
    for (const auto& attr : attributes_) {
        attr_length += 4 + attr.value.size(); // Type(2) + Length(2) + Value
        // Add padding to 4-byte boundary
        if (attr.value.size() % 4 != 0) {
            attr_length += 4 - (attr.value.size() % 4);
        }
    }
    
    // STUN header (20 bytes)
    result.resize(20 + attr_length);
    
    // Message type and length
    *reinterpret_cast<uint16_t*>(result.data()) = htons(static_cast<uint16_t>(type_));
    *reinterpret_cast<uint16_t*>(result.data() + 2) = htons(static_cast<uint16_t>(attr_length));
    
    // Magic cookie
    *reinterpret_cast<uint32_t*>(result.data() + 4) = htonl(STUN_MAGIC_COOKIE);
    
    // Transaction ID
    std::memcpy(result.data() + 8, transaction_id_.data(), 12);
    
    // Attributes
    size_t offset = 20;
    for (const auto& attr : attributes_) {
        *reinterpret_cast<uint16_t*>(result.data() + offset) = htons(static_cast<uint16_t>(attr.type));
        *reinterpret_cast<uint16_t*>(result.data() + offset + 2) = htons(static_cast<uint16_t>(attr.value.size()));
        
        if (!attr.value.empty()) {
            std::memcpy(result.data() + offset + 4, attr.value.data(), attr.value.size());
        }
        
        offset += 4 + attr.value.size();
        
        // Add padding
        while (offset % 4 != 0) {
            result[offset++] = 0;
        }
    }
    
    return result;
}

bool StunMessage::parse(const uint8_t* data, size_t size) {
    if (size < 20) {
        return false; // Too small for STUN header
    }
    
    // Parse header
    type_ = static_cast<StunMessageType>(ntohs(*reinterpret_cast<const uint16_t*>(data)));
    uint16_t length = ntohs(*reinterpret_cast<const uint16_t*>(data + 2));
    uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(data + 4));
    
    if (magic != STUN_MAGIC_COOKIE) {
        return false; // Invalid magic cookie
    }
    
    if (size < 20 + length) {
        return false; // Message too short
    }
    
    // Extract transaction ID
    transaction_id_.assign(data + 8, data + 20);
    
    // Parse attributes
    attributes_.clear();
    size_t offset = 20;
    
    while (offset < 20 + length) {
        if (offset + 4 > size) break;
        
        StunAttributeType attr_type = static_cast<StunAttributeType>(ntohs(*reinterpret_cast<const uint16_t*>(data + offset)));
        uint16_t attr_length = ntohs(*reinterpret_cast<const uint16_t*>(data + offset + 2));
        
        if (offset + 4 + attr_length > size) break;
        
        std::vector<uint8_t> attr_value(data + offset + 4, data + offset + 4 + attr_length);
        attributes_.emplace_back(attr_type, attr_value);
        
        offset += 4 + attr_length;
        
        // Skip padding
        while (offset % 4 != 0 && offset < size) {
            offset++;
        }
    }
    
    return true;
}

bool StunMessage::isValid() const {
    return !transaction_id_.empty() && transaction_id_.size() == 12;
}

std::vector<uint8_t> StunMessage::generateTransactionId() {
    std::vector<uint8_t> id(12);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : id) {
        byte = dis(gen);
    }
    
    return id;
}

bool StunMessage::isStunMessage(const uint8_t* data, size_t size) {
    if (size < 20) return false;
    
    uint32_t magic = ntohl(*reinterpret_cast<const uint32_t*>(data + 4));
    return magic == STUN_MAGIC_COOKIE;
}

// Placeholder implementations for cryptographic functions
bool StunMessage::verifyMessageIntegrity(const std::string& key) const {
    // TODO: Implement HMAC-SHA1 verification
    return true;
}

void StunMessage::addMessageIntegrity(const std::string& key) {
    // TODO: Implement HMAC-SHA1 calculation
    std::vector<uint8_t> hmac(20, 0x42); // Placeholder
    addAttribute(StunAttributeType::MESSAGE_INTEGRITY, hmac);
}

void StunMessage::addFingerprint() {
    // TODO: Implement CRC32 fingerprint
    addAttribute(StunAttributeType::FINGERPRINT, static_cast<uint32_t>(0x12345678));
}

bool StunMessage::verifyFingerprint() const {
    // TODO: Implement CRC32 verification
    return hasAttribute(StunAttributeType::FINGERPRINT);
}

uint32_t StunMessage::calculateCrc32(const std::vector<uint8_t>& data) const {
    // TODO: Implement CRC32 calculation
    return 0x12345678;
}

std::vector<uint8_t> StunMessage::calculateHmacSha1(const std::vector<uint8_t>& data, const std::string& key) const {
    // TODO: Implement HMAC-SHA1 calculation
    return std::vector<uint8_t>(20, 0x42);
}

// StunClient implementation
StunClient::StunClient() : running_(false) {
}

StunClient::~StunClient() {
    stop();
}

bool StunClient::start(const SocketAddress& local_address) {
    if (running_) {
        return true;
    }

    socket_ = createUdpSocket();
    if (!socket_) {
        return false;
    }

    socket_->setDataCallback([this](const std::vector<uint8_t>& data, const SocketAddress& from) {
        onSocketData(data, from);
    });

    socket_->setErrorCallback([this](const std::string& error) {
        onSocketError(error);
    });

    if (!socket_->bind(local_address)) {
        socket_.reset();
        return false;
    }

    socket_->startReceiving();
    running_ = true;

    core::Logger::info("STUN client started on {}", local_address.toString());
    return true;
}

void StunClient::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (socket_) {
        socket_->close();
        socket_.reset();
    }

    core::Logger::info("STUN client stopped");
}

bool StunClient::sendBindingRequest() {
    if (!running_ || stun_server_.port == 0) {
        return false;
    }

    StunMessage request(StunMessageType::BINDING_REQUEST);

    // Add software attribute
    request.addAttribute(StunAttributeType::SOFTWARE, "FMUS-3G");

    // Add credentials if available
    if (!username_.empty()) {
        request.addAttribute(StunAttributeType::USERNAME, username_);
    }

    return sendMessage(request, stun_server_);
}

bool StunClient::sendMessage(const StunMessage& message, const SocketAddress& destination) {
    if (!running_ || !socket_) {
        return false;
    }

    auto data = message.serialize();
    bool success = socket_->send(data, destination);

    if (success) {
        core::Logger::debug("Sent STUN message type {} to {}",
                           static_cast<int>(message.getType()), destination.toString());
    } else {
        core::Logger::error("Failed to send STUN message to {}", destination.toString());
    }

    return success;
}

void StunClient::onSocketData(const std::vector<uint8_t>& data, const SocketAddress& from) {
    if (!StunMessage::isStunMessage(data.data(), data.size())) {
        core::Logger::debug("Received non-STUN data from {}", from.toString());
        return;
    }

    StunMessage message(data.data(), data.size());
    if (!message.isValid()) {
        core::Logger::warn("Received invalid STUN message from {}", from.toString());
        return;
    }

    core::Logger::debug("Received STUN message type {} from {}",
                       static_cast<int>(message.getType()), from.toString());

    processStunMessage(message, from);

    if (response_callback_) {
        response_callback_(message, from);
    }
}

void StunClient::onSocketError(const std::string& error) {
    core::Logger::error("STUN client socket error: {}", error);

    if (error_callback_) {
        error_callback_(error);
    }
}

void StunClient::processStunMessage(const StunMessage& message, const SocketAddress& from) {
    switch (message.getType()) {
        case StunMessageType::BINDING_RESPONSE: {
            // Extract mapped address
            if (message.hasAttribute(StunAttributeType::XOR_MAPPED_ADDRESS)) {
                auto attr = message.getAttribute(StunAttributeType::XOR_MAPPED_ADDRESS);
                mapped_address_ = attr.asXorAddress(message.getTransactionId());

                core::Logger::info("STUN binding response: mapped address is {}",
                                  mapped_address_.toString());

                if (binding_callback_) {
                    binding_callback_(mapped_address_);
                }
            } else if (message.hasAttribute(StunAttributeType::MAPPED_ADDRESS)) {
                auto attr = message.getAttribute(StunAttributeType::MAPPED_ADDRESS);
                mapped_address_ = attr.asAddress();

                core::Logger::info("STUN binding response: mapped address is {}",
                                  mapped_address_.toString());

                if (binding_callback_) {
                    binding_callback_(mapped_address_);
                }
            }
            break;
        }

        case StunMessageType::BINDING_ERROR_RESPONSE: {
            if (message.hasAttribute(StunAttributeType::ERROR_CODE)) {
                auto attr = message.getAttribute(StunAttributeType::ERROR_CODE);
                if (attr.value.size() >= 4) {
                    uint16_t error_code = ntohs(*reinterpret_cast<const uint16_t*>(attr.value.data() + 2));
                    std::string reason(attr.value.begin() + 4, attr.value.end());

                    core::Logger::error("STUN binding error {}: {}", error_code, reason);

                    if (error_callback_) {
                        error_callback_("STUN error " + std::to_string(error_code) + ": " + reason);
                    }
                }
            }
            break;
        }

        default:
            core::Logger::debug("Unhandled STUN message type: {}", static_cast<int>(message.getType()));
            break;
    }
}

// IceCandidate implementation
std::string IceCandidate::toString() const {
    std::ostringstream oss;
    oss << "candidate:" << foundation << " " << component_id << " " << transport << " "
        << priority << " " << address.toString();

    switch (type) {
        case IceCandidateType::HOST:
            oss << " typ host";
            break;
        case IceCandidateType::SERVER_REFLEXIVE:
            oss << " typ srflx";
            if (related_address.port != 0) {
                oss << " raddr " << related_address.ip << " rport " << related_address.port;
            }
            break;
        case IceCandidateType::PEER_REFLEXIVE:
            oss << " typ prflx";
            if (related_address.port != 0) {
                oss << " raddr " << related_address.ip << " rport " << related_address.port;
            }
            break;
        case IceCandidateType::RELAY:
            oss << " typ relay";
            if (related_address.port != 0) {
                oss << " raddr " << related_address.ip << " rport " << related_address.port;
            }
            break;
    }

    return oss.str();
}

IceCandidate IceCandidate::fromString(const std::string& candidate_line) {
    IceCandidate candidate;
    std::istringstream iss(candidate_line);
    std::string token;

    // Skip "candidate:" prefix
    iss >> token; // "candidate:foundation"
    if (token.substr(0, 10) == "candidate:") {
        candidate.foundation = token.substr(10);
    }

    iss >> candidate.component_id >> candidate.transport >> candidate.priority;

    std::string ip;
    uint16_t port;
    iss >> ip >> port;
    candidate.address = SocketAddress(ip, port);

    // Parse type and related address
    while (iss >> token) {
        if (token == "typ") {
            iss >> token;
            if (token == "host") candidate.type = IceCandidateType::HOST;
            else if (token == "srflx") candidate.type = IceCandidateType::SERVER_REFLEXIVE;
            else if (token == "prflx") candidate.type = IceCandidateType::PEER_REFLEXIVE;
            else if (token == "relay") candidate.type = IceCandidateType::RELAY;
        } else if (token == "raddr") {
            iss >> ip;
            candidate.related_address.ip = ip;
        } else if (token == "rport") {
            iss >> port;
            candidate.related_address.port = port;
        }
    }

    return candidate;
}

uint32_t IceCandidate::calculatePriority(IceCandidateType type, uint16_t local_pref, uint8_t component_id) {
    uint8_t type_pref;
    switch (type) {
        case IceCandidateType::HOST: type_pref = 126; break;
        case IceCandidateType::PEER_REFLEXIVE: type_pref = 110; break;
        case IceCandidateType::SERVER_REFLEXIVE: type_pref = 100; break;
        case IceCandidateType::RELAY: type_pref = 0; break;
        default: type_pref = 0; break;
    }

    return (type_pref << 24) | (local_pref << 8) | (256 - component_id);
}

// Simple IceAgent implementation
IceAgent::IceAgent() : connected_(false) {
}

IceAgent::~IceAgent() {
    stop();
}

void IceAgent::setStunServer(const SocketAddress& server) {
    if (!stun_client_) {
        stun_client_ = std::make_unique<StunClient>();
    }
    stun_client_->setStunServer(server);
}

void IceAgent::setTurnServer(const SocketAddress& server, const std::string& username, const std::string& password) {
    if (!turn_client_) {
        turn_client_ = std::make_unique<TurnClient>();
    }
    turn_client_->setTurnServer(server);
    turn_client_->setCredentials(username, password);
}

bool IceAgent::start(const SocketAddress& local_address) {
    // Start STUN client if configured
    if (stun_client_) {
        stun_client_->setBindingCallback([this](const SocketAddress& mapped_address) {
            onStunBinding(mapped_address);
        });

        if (!stun_client_->start(local_address)) {
            core::Logger::error("Failed to start STUN client");
            return false;
        }
    }

    // Start TURN client if configured
    if (turn_client_) {
        turn_client_->setAllocationCallback([this](const SocketAddress& relayed_address, uint32_t lifetime) {
            onTurnAllocation(relayed_address, lifetime);
        });

        SocketAddress turn_local = local_address;
        turn_local.port += 1; // Use different port for TURN

        if (!turn_client_->start(turn_local)) {
            core::Logger::error("Failed to start TURN client");
            return false;
        }
    }

    // Gather candidates
    gatherCandidates();

    core::Logger::info("ICE agent started");
    return true;
}

void IceAgent::stop() {
    if (stun_client_) {
        stun_client_->stop();
    }

    if (turn_client_) {
        turn_client_->stop();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    local_candidates_.clear();
    remote_candidates_.clear();
    connected_ = false;

    core::Logger::info("ICE agent stopped");
}

void IceAgent::addRemoteCandidate(const IceCandidate& candidate) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_candidates_.push_back(candidate);

    core::Logger::info("Added remote ICE candidate: {}", candidate.toString());
}

void IceAgent::startConnectivityChecks() {
    // Simple implementation - just mark as connected if we have candidates
    std::lock_guard<std::mutex> lock(mutex_);

    if (!local_candidates_.empty() && !remote_candidates_.empty()) {
        connected_ = true;

        if (connectivity_callback_) {
            connectivity_callback_(local_candidates_[0], remote_candidates_[0]);
        }

        core::Logger::info("ICE connectivity established");
    }
}

void IceAgent::gatherCandidates() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Add host candidate (placeholder - would normally get from socket)
    IceCandidate host_candidate;
    host_candidate.foundation = "1";
    host_candidate.component_id = 1;
    host_candidate.transport = "udp";
    host_candidate.type = IceCandidateType::HOST;
    host_candidate.address = SocketAddress("192.168.1.100", 5000); // Placeholder
    host_candidate.priority = host_candidate.calculatePriority(IceCandidateType::HOST);

    local_candidates_.push_back(host_candidate);

    if (candidate_callback_) {
        candidate_callback_(host_candidate);
    }

    // Request STUN binding to get server reflexive candidate
    if (stun_client_) {
        stun_client_->sendBindingRequest();
    }

    // Request TURN allocation to get relay candidate
    if (turn_client_) {
        turn_client_->allocate();
    }
}

void IceAgent::onStunBinding(const SocketAddress& mapped_address) {
    std::lock_guard<std::mutex> lock(mutex_);

    IceCandidate srflx_candidate;
    srflx_candidate.foundation = "2";
    srflx_candidate.component_id = 1;
    srflx_candidate.transport = "udp";
    srflx_candidate.type = IceCandidateType::SERVER_REFLEXIVE;
    srflx_candidate.address = mapped_address;
    srflx_candidate.related_address = SocketAddress("192.168.1.100", 5000); // Host address
    srflx_candidate.priority = srflx_candidate.calculatePriority(IceCandidateType::SERVER_REFLEXIVE);

    local_candidates_.push_back(srflx_candidate);

    if (candidate_callback_) {
        candidate_callback_(srflx_candidate);
    }

    core::Logger::info("Gathered server reflexive candidate: {}", srflx_candidate.toString());
}

void IceAgent::onTurnAllocation(const SocketAddress& relayed_address, uint32_t lifetime) {
    std::lock_guard<std::mutex> lock(mutex_);

    IceCandidate relay_candidate;
    relay_candidate.foundation = "3";
    relay_candidate.component_id = 1;
    relay_candidate.transport = "udp";
    relay_candidate.type = IceCandidateType::RELAY;
    relay_candidate.address = relayed_address;
    relay_candidate.related_address = SocketAddress("192.168.1.100", 5001); // TURN client address
    relay_candidate.priority = relay_candidate.calculatePriority(IceCandidateType::RELAY);

    local_candidates_.push_back(relay_candidate);

    if (candidate_callback_) {
        candidate_callback_(relay_candidate);
    }

    core::Logger::info("Gathered relay candidate: {} (lifetime: {}s)",
                      relay_candidate.toString(), lifetime);
}

// TurnClient stub implementation
TurnClient::TurnClient() : allocated_(false), allocation_lifetime_(0) {
}

TurnClient::~TurnClient() {
}

bool TurnClient::allocate(uint32_t lifetime) {
    // Stub implementation - simulate successful allocation
    allocated_ = true;
    allocation_lifetime_ = lifetime;
    relayed_address_ = SocketAddress("203.0.113.1", 49152); // Example relay address

    if (allocation_callback_) {
        allocation_callback_(relayed_address_, allocation_lifetime_);
    }

    core::Logger::info("TURN allocation successful: {} (lifetime: {}s)",
                      relayed_address_.toString(), lifetime);
    return true;
}

bool TurnClient::refresh(uint32_t lifetime) {
    if (!allocated_) return false;

    allocation_lifetime_ = lifetime;
    core::Logger::info("TURN allocation refreshed: lifetime {}s", lifetime);
    return true;
}

bool TurnClient::createPermission(const SocketAddress& peer) {
    if (!allocated_) return false;

    core::Logger::info("TURN permission created for {}", peer.toString());
    return true;
}

bool TurnClient::sendData(const std::vector<uint8_t>& data, const SocketAddress& peer) {
    if (!allocated_) return false;

    core::Logger::debug("TURN data sent to {}: {} bytes", peer.toString(), data.size());
    return true;
}

void TurnClient::deallocate() {
    allocated_ = false;
    allocation_lifetime_ = 0;
    relayed_address_ = SocketAddress();

    core::Logger::info("TURN allocation deallocated");
}

} // namespace fmus::network
