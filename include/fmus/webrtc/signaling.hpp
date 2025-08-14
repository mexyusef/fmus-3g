#pragma once

#include "../network/socket.hpp"
#include "../sip/sdp.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>

namespace fmus::webrtc {

// WebRTC Signaling Message Types
enum class SignalingMessageType {
    OFFER,
    ANSWER,
    ICE_CANDIDATE,
    BYE,
    ERROR,
    PING,
    PONG
};

// WebRTC Signaling Message
struct SignalingMessage {
    SignalingMessageType type;
    std::string session_id;
    std::string from;
    std::string to;
    std::string data; // JSON payload
    
    SignalingMessage() = default;
    SignalingMessage(SignalingMessageType t, const std::string& sid, 
                    const std::string& f, const std::string& t_to, const std::string& d)
        : type(t), session_id(sid), from(f), to(t_to), data(d) {}
    
    std::string toJson() const;
    static SignalingMessage fromJson(const std::string& json);
    
    bool isValid() const { return !session_id.empty() && !from.empty(); }
};

// WebSocket Frame Types (simplified)
enum class WebSocketOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

// WebSocket Frame
struct WebSocketFrame {
    bool fin = true;
    WebSocketOpcode opcode = WebSocketOpcode::TEXT;
    bool masked = false;
    uint32_t mask = 0;
    std::vector<uint8_t> payload;
    
    std::vector<uint8_t> serialize() const;
    static WebSocketFrame deserialize(const uint8_t* data, size_t size);
    static bool isWebSocketFrame(const uint8_t* data, size_t size);
};

// WebSocket Connection
class WebSocketConnection {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using CloseCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;

    WebSocketConnection(std::shared_ptr<network::TcpSocket> socket);
    ~WebSocketConnection();
    
    // Connection management
    bool performHandshake(const std::string& request);
    void close();
    bool isConnected() const { return connected_; }
    
    // Message handling
    bool sendMessage(const std::string& message);
    bool sendPing();
    bool sendPong();
    
    // Callbacks
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void setCloseCallback(CloseCallback callback) { close_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Properties
    const std::string& getId() const { return connection_id_; }
    const network::SocketAddress& getRemoteAddress() const;

private:
    void onSocketData(const std::vector<uint8_t>& data, const network::SocketAddress& from);
    void onSocketError(const std::string& error);
    void processWebSocketFrame(const WebSocketFrame& frame);
    
    std::string generateWebSocketAccept(const std::string& key) const;
    std::string calculateSHA1(const std::string& data) const;
    std::string base64Encode(const std::vector<uint8_t>& data) const;
    
    std::shared_ptr<network::TcpSocket> socket_;
    std::string connection_id_;
    std::atomic<bool> connected_;
    std::atomic<bool> handshake_complete_;
    
    // Frame assembly
    std::vector<uint8_t> frame_buffer_;
    
    MessageCallback message_callback_;
    CloseCallback close_callback_;
    ErrorCallback error_callback_;
    
    mutable std::mutex mutex_;
};

// WebRTC Signaling Server
class SignalingServer {
public:
    using ClientConnectedCallback = std::function<void(const std::string&)>; // connection_id
    using ClientDisconnectedCallback = std::function<void(const std::string&)>; // connection_id
    using MessageCallback = std::function<void(const SignalingMessage&, const std::string&)>; // message, connection_id

    SignalingServer();
    ~SignalingServer();
    
    // Server management
    bool start(const network::SocketAddress& bind_address);
    void stop();
    bool isRunning() const { return running_; }
    
    // Message handling
    bool sendMessage(const SignalingMessage& message, const std::string& connection_id);
    bool broadcastMessage(const SignalingMessage& message);
    bool sendToSession(const SignalingMessage& message, const std::string& session_id);
    
    // Connection management
    void disconnectClient(const std::string& connection_id);
    std::vector<std::string> getConnectedClients() const;
    size_t getConnectionCount() const;
    
    // Session management
    void createSession(const std::string& session_id, const std::string& creator_id);
    void joinSession(const std::string& session_id, const std::string& client_id);
    void leaveSession(const std::string& session_id, const std::string& client_id);
    void destroySession(const std::string& session_id);
    
    std::vector<std::string> getSessionParticipants(const std::string& session_id) const;
    std::vector<std::string> getActiveSessions() const;
    
    // Callbacks
    void setClientConnectedCallback(ClientConnectedCallback callback) { client_connected_callback_ = callback; }
    void setClientDisconnectedCallback(ClientDisconnectedCallback callback) { client_disconnected_callback_ = callback; }
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    // Statistics
    struct Stats {
        uint64_t connections_accepted = 0;
        uint64_t connections_closed = 0;
        uint64_t messages_sent = 0;
        uint64_t messages_received = 0;
        uint64_t sessions_created = 0;
        uint64_t sessions_destroyed = 0;
        uint64_t errors = 0;
    };
    
    Stats getStats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    void onNewConnection(std::shared_ptr<network::Socket> connection);
    void onConnectionMessage(const std::string& message, const std::string& connection_id);
    void onConnectionClosed(const std::string& connection_id);
    void onConnectionError(const std::string& error, const std::string& connection_id);
    
    void processSignalingMessage(const SignalingMessage& message, const std::string& connection_id);
    void handleOffer(const SignalingMessage& message, const std::string& connection_id);
    void handleAnswer(const SignalingMessage& message, const std::string& connection_id);
    void handleIceCandidate(const SignalingMessage& message, const std::string& connection_id);
    void handleBye(const SignalingMessage& message, const std::string& connection_id);
    
    std::shared_ptr<network::TcpSocket> server_socket_;
    std::atomic<bool> running_;
    
    // Connection management
    std::unordered_map<std::string, std::shared_ptr<WebSocketConnection>> connections_;
    
    // Session management
    struct Session {
        std::string id;
        std::string creator_id;
        std::vector<std::string> participants;
        std::chrono::system_clock::time_point created;
    };
    
    std::unordered_map<std::string, Session> sessions_;
    std::unordered_map<std::string, std::string> client_to_session_; // client_id -> session_id
    
    // Callbacks
    ClientConnectedCallback client_connected_callback_;
    ClientDisconnectedCallback client_disconnected_callback_;
    MessageCallback message_callback_;
    
    // Statistics
    Stats stats_;
    
    // Synchronization
    mutable std::mutex mutex_;
};

// WebRTC Signaling Client
class SignalingClient {
public:
    using ConnectedCallback = std::function<void()>;
    using DisconnectedCallback = std::function<void()>;
    using MessageCallback = std::function<void(const SignalingMessage&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    SignalingClient();
    ~SignalingClient();
    
    // Connection management
    bool connect(const network::SocketAddress& server_address);
    void disconnect();
    bool isConnected() const { return connected_; }
    
    // Session management
    bool createSession(const std::string& session_id);
    bool joinSession(const std::string& session_id);
    bool leaveSession();
    
    // Message sending
    bool sendOffer(const std::string& to, const sip::SessionDescription& sdp);
    bool sendAnswer(const std::string& to, const sip::SessionDescription& sdp);
    bool sendIceCandidate(const std::string& to, const std::string& candidate);
    bool sendBye(const std::string& to);
    
    // Callbacks
    void setConnectedCallback(ConnectedCallback callback) { connected_callback_ = callback; }
    void setDisconnectedCallback(DisconnectedCallback callback) { disconnected_callback_ = callback; }
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Properties
    const std::string& getClientId() const { return client_id_; }
    const std::string& getCurrentSession() const { return current_session_; }

private:
    void onWebSocketMessage(const std::string& message);
    void onWebSocketClosed();
    void onWebSocketError(const std::string& error);
    
    bool sendMessage(const SignalingMessage& message);
    
    std::unique_ptr<WebSocketConnection> connection_;
    std::string client_id_;
    std::string current_session_;
    std::atomic<bool> connected_;
    
    ConnectedCallback connected_callback_;
    DisconnectedCallback disconnected_callback_;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    
    mutable std::mutex mutex_;
};

// Utility functions
std::string generateSessionId();
std::string generateClientId();
std::string signalingMessageTypeToString(SignalingMessageType type);
SignalingMessageType stringToSignalingMessageType(const std::string& type);

} // namespace fmus::webrtc
