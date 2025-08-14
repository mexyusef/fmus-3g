#pragma once

#include "socket.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>

namespace fmus::network {

// STUN Message Types (RFC 5389)
enum class StunMessageType : uint16_t {
    // Binding method
    BINDING_REQUEST = 0x0001,
    BINDING_RESPONSE = 0x0101,
    BINDING_ERROR_RESPONSE = 0x0111,
    BINDING_INDICATION = 0x0011,
    
    // Allocate method (TURN)
    ALLOCATE_REQUEST = 0x0003,
    ALLOCATE_RESPONSE = 0x0103,
    ALLOCATE_ERROR_RESPONSE = 0x0113,
    
    // Refresh method (TURN)
    REFRESH_REQUEST = 0x0004,
    REFRESH_RESPONSE = 0x0104,
    REFRESH_ERROR_RESPONSE = 0x0114,
    
    // Send method (TURN)
    SEND_INDICATION = 0x0016,
    
    // Data method (TURN)
    DATA_INDICATION = 0x0017,
    
    // CreatePermission method (TURN)
    CREATE_PERMISSION_REQUEST = 0x0008,
    CREATE_PERMISSION_RESPONSE = 0x0108,
    CREATE_PERMISSION_ERROR_RESPONSE = 0x0118,
    
    // ChannelBind method (TURN)
    CHANNEL_BIND_REQUEST = 0x0009,
    CHANNEL_BIND_RESPONSE = 0x0109,
    CHANNEL_BIND_ERROR_RESPONSE = 0x0119
};

// STUN Attribute Types (RFC 5389 and RFC 5766)
enum class StunAttributeType : uint16_t {
    MAPPED_ADDRESS = 0x0001,
    USERNAME = 0x0006,
    MESSAGE_INTEGRITY = 0x0008,
    ERROR_CODE = 0x0009,
    UNKNOWN_ATTRIBUTES = 0x000A,
    REALM = 0x0014,
    NONCE = 0x0015,
    XOR_MAPPED_ADDRESS = 0x0020,
    
    // TURN attributes
    CHANNEL_NUMBER = 0x000C,
    LIFETIME = 0x000D,
    XOR_PEER_ADDRESS = 0x0012,
    DATA = 0x0013,
    XOR_RELAYED_ADDRESS = 0x0016,
    EVEN_PORT = 0x0018,
    REQUESTED_TRANSPORT = 0x0019,
    DONT_FRAGMENT = 0x001A,
    RESERVATION_TOKEN = 0x0022,
    
    // ICE attributes
    PRIORITY = 0x0024,
    USE_CANDIDATE = 0x0025,
    ICE_CONTROLLED = 0x8029,
    ICE_CONTROLLING = 0x802A,
    
    // Software and fingerprint
    SOFTWARE = 0x8022,
    FINGERPRINT = 0x8028
};

// STUN Error Codes
enum class StunErrorCode : uint16_t {
    TRY_ALTERNATE = 300,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    UNKNOWN_ATTRIBUTE = 420,
    STALE_NONCE = 438,
    SERVER_ERROR = 500,
    
    // TURN specific
    FORBIDDEN = 403,
    ALLOCATION_MISMATCH = 437,
    STALE_CREDENTIALS = 438,
    WRONG_CREDENTIALS = 441,
    UNSUPPORTED_TRANSPORT = 442,
    ALLOCATION_QUOTA_REACHED = 486,
    INSUFFICIENT_CAPACITY = 508
};

// STUN Attribute
struct StunAttribute {
    StunAttributeType type;
    std::vector<uint8_t> value;
    
    StunAttribute(StunAttributeType t, const std::vector<uint8_t>& v) : type(t), value(v) {}
    StunAttribute(StunAttributeType t, const std::string& s) : type(t), value(s.begin(), s.end()) {}
    
    std::string asString() const { return std::string(value.begin(), value.end()); }
    uint32_t asUint32() const;
    uint16_t asUint16() const;
    SocketAddress asAddress() const;
    SocketAddress asXorAddress(const std::vector<uint8_t>& transaction_id) const;
};

// STUN Message
class StunMessage {
public:
    StunMessage(StunMessageType type);
    StunMessage(const uint8_t* data, size_t size);
    
    // Basic properties
    StunMessageType getType() const { return type_; }
    const std::vector<uint8_t>& getTransactionId() const { return transaction_id_; }
    
    // Attributes
    void addAttribute(const StunAttribute& attr);
    void addAttribute(StunAttributeType type, const std::vector<uint8_t>& value);
    void addAttribute(StunAttributeType type, const std::string& value);
    void addAttribute(StunAttributeType type, uint32_t value);
    void addAttribute(StunAttributeType type, uint16_t value);
    void addAddressAttribute(StunAttributeType type, const SocketAddress& address);
    void addXorAddressAttribute(StunAttributeType type, const SocketAddress& address);
    
    bool hasAttribute(StunAttributeType type) const;
    StunAttribute getAttribute(StunAttributeType type) const;
    std::vector<StunAttribute> getAttributes(StunAttributeType type) const;
    void removeAttribute(StunAttributeType type);
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    bool parse(const uint8_t* data, size_t size);
    
    // Validation
    bool isValid() const;
    bool verifyMessageIntegrity(const std::string& key) const;
    void addMessageIntegrity(const std::string& key);
    void addFingerprint();
    bool verifyFingerprint() const;
    
    // Utility
    static std::vector<uint8_t> generateTransactionId();
    static bool isStunMessage(const uint8_t* data, size_t size);

private:
    StunMessageType type_;
    std::vector<uint8_t> transaction_id_;
    std::vector<StunAttribute> attributes_;
    
    uint32_t calculateCrc32(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> calculateHmacSha1(const std::vector<uint8_t>& data, const std::string& key) const;
};

// STUN Client
class StunClient {
public:
    using ResponseCallback = std::function<void(const StunMessage&, const SocketAddress&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using BindingCallback = std::function<void(const SocketAddress&)>; // Mapped address

    StunClient();
    ~StunClient();
    
    // Configuration
    void setStunServer(const SocketAddress& server) { stun_server_ = server; }
    void setCredentials(const std::string& username, const std::string& password) {
        username_ = username;
        password_ = password;
    }
    
    // Callbacks
    void setResponseCallback(ResponseCallback callback) { response_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    void setBindingCallback(BindingCallback callback) { binding_callback_ = callback; }
    
    // Operations
    bool start(const SocketAddress& local_address);
    void stop();
    
    bool sendBindingRequest();
    bool sendMessage(const StunMessage& message, const SocketAddress& destination);
    
    // State
    bool isRunning() const { return running_; }
    const SocketAddress& getMappedAddress() const { return mapped_address_; }
    const SocketAddress& getStunServer() const { return stun_server_; }

private:
    void onSocketData(const std::vector<uint8_t>& data, const SocketAddress& from);
    void onSocketError(const std::string& error);
    void processStunMessage(const StunMessage& message, const SocketAddress& from);
    
    std::shared_ptr<UdpSocket> socket_;
    SocketAddress stun_server_;
    SocketAddress mapped_address_;
    
    std::string username_;
    std::string password_;
    
    std::atomic<bool> running_;
    
    ResponseCallback response_callback_;
    ErrorCallback error_callback_;
    BindingCallback binding_callback_;
    
    mutable std::mutex mutex_;
};

// TURN Client (extends STUN for relay functionality)
class TurnClient : public StunClient {
public:
    using AllocationCallback = std::function<void(const SocketAddress&, uint32_t)>; // Relayed address, lifetime
    using DataCallback = std::function<void(const std::vector<uint8_t>&, const SocketAddress&)>;

    TurnClient();
    ~TurnClient();
    
    // TURN-specific configuration
    void setTurnServer(const SocketAddress& server) { setStunServer(server); }
    void setAllocationCallback(AllocationCallback callback) { allocation_callback_ = callback; }
    void setDataCallback(DataCallback callback) { data_callback_ = callback; }
    
    // TURN operations
    bool allocate(uint32_t lifetime = 600); // Default 10 minutes
    bool refresh(uint32_t lifetime = 600);
    bool createPermission(const SocketAddress& peer);
    bool sendData(const std::vector<uint8_t>& data, const SocketAddress& peer);
    void deallocate();
    
    // State
    bool isAllocated() const { return allocated_; }
    const SocketAddress& getRelayedAddress() const { return relayed_address_; }
    uint32_t getAllocationLifetime() const { return allocation_lifetime_; }

private:
    void processTurnMessage(const StunMessage& message, const SocketAddress& from);
    
    std::atomic<bool> allocated_;
    SocketAddress relayed_address_;
    uint32_t allocation_lifetime_;
    
    AllocationCallback allocation_callback_;
    DataCallback data_callback_;
};

// ICE Candidate Types
enum class IceCandidateType {
    HOST,
    SERVER_REFLEXIVE,
    PEER_REFLEXIVE,
    RELAY
};

// ICE Candidate
struct IceCandidate {
    std::string foundation;
    uint32_t component_id;
    std::string transport; // "udp" or "tcp"
    uint32_t priority;
    SocketAddress address;
    IceCandidateType type;
    SocketAddress related_address; // For reflexive and relay candidates
    
    std::string toString() const;
    static IceCandidate fromString(const std::string& candidate_line);
    
    uint32_t calculatePriority(IceCandidateType type, uint16_t local_pref = 65535, uint8_t component_id = 1);
};

// Simple ICE Agent (basic implementation)
class IceAgent {
public:
    using CandidateCallback = std::function<void(const IceCandidate&)>;
    using ConnectivityCallback = std::function<void(const IceCandidate&, const IceCandidate&)>;

    IceAgent();
    ~IceAgent();
    
    // Configuration
    void setStunServer(const SocketAddress& server);
    void setTurnServer(const SocketAddress& server, const std::string& username, const std::string& password);
    
    // Callbacks
    void setCandidateCallback(CandidateCallback callback) { candidate_callback_ = callback; }
    void setConnectivityCallback(ConnectivityCallback callback) { connectivity_callback_ = callback; }
    
    // Operations
    bool start(const SocketAddress& local_address);
    void stop();
    
    void addRemoteCandidate(const IceCandidate& candidate);
    void startConnectivityChecks();
    
    // State
    const std::vector<IceCandidate>& getLocalCandidates() const { return local_candidates_; }
    const std::vector<IceCandidate>& getRemoteCandidates() const { return remote_candidates_; }
    bool isConnected() const { return connected_; }

private:
    void gatherCandidates();
    void onStunBinding(const SocketAddress& mapped_address);
    void onTurnAllocation(const SocketAddress& relayed_address, uint32_t lifetime);
    
    std::unique_ptr<StunClient> stun_client_;
    std::unique_ptr<TurnClient> turn_client_;
    
    std::vector<IceCandidate> local_candidates_;
    std::vector<IceCandidate> remote_candidates_;
    
    std::atomic<bool> connected_;
    
    CandidateCallback candidate_callback_;
    ConnectivityCallback connectivity_callback_;
    
    mutable std::mutex mutex_;
};

} // namespace fmus::network
