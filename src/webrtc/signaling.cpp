#include "fmus/webrtc/signaling.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <random>
#include <algorithm>
#include <regex>
#include <cstring>

namespace fmus::webrtc {

// SignalingMessage implementation
std::string SignalingMessage::toJson() const {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"" << signalingMessageTypeToString(type) << "\","
        << "\"sessionId\":\"" << session_id << "\","
        << "\"from\":\"" << from << "\"";
    
    if (!to.empty()) {
        oss << ",\"to\":\"" << to << "\"";
    }
    
    if (!data.empty()) {
        oss << ",\"data\":" << data; // Assume data is already JSON
    }
    
    oss << "}";
    return oss.str();
}

SignalingMessage SignalingMessage::fromJson(const std::string& json) {
    SignalingMessage message;

    // Simple JSON parsing - would use proper JSON library in production
    // Using simple string search instead of regex for now
    size_t type_pos = json.find("\"type\":");
    if (type_pos != std::string::npos) {
        size_t start = json.find("\"", type_pos + 7);
        if (start != std::string::npos) {
            size_t end = json.find("\"", start + 1);
            if (end != std::string::npos) {
                message.type = stringToSignalingMessageType(json.substr(start + 1, end - start - 1));
            }
        }
    }

    size_t session_pos = json.find("\"sessionId\":");
    if (session_pos != std::string::npos) {
        size_t start = json.find("\"", session_pos + 12);
        if (start != std::string::npos) {
            size_t end = json.find("\"", start + 1);
            if (end != std::string::npos) {
                message.session_id = json.substr(start + 1, end - start - 1);
            }
        }
    }

    size_t from_pos = json.find("\"from\":");
    if (from_pos != std::string::npos) {
        size_t start = json.find("\"", from_pos + 7);
        if (start != std::string::npos) {
            size_t end = json.find("\"", start + 1);
            if (end != std::string::npos) {
                message.from = json.substr(start + 1, end - start - 1);
            }
        }
    }

    size_t to_pos = json.find("\"to\":");
    if (to_pos != std::string::npos) {
        size_t start = json.find("\"", to_pos + 5);
        if (start != std::string::npos) {
            size_t end = json.find("\"", start + 1);
            if (end != std::string::npos) {
                message.to = json.substr(start + 1, end - start - 1);
            }
        }
    }

    size_t data_pos = json.find("\"data\":");
    if (data_pos != std::string::npos) {
        size_t start = data_pos + 7;
        // Skip whitespace
        while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) {
            start++;
        }
        if (start < json.length()) {
            if (json[start] == '"') {
                // String value
                size_t end = json.find("\"", start + 1);
                if (end != std::string::npos) {
                    message.data = json.substr(start, end - start + 1);
                }
            } else if (json[start] == '{') {
                // Object value - find matching brace
                int brace_count = 1;
                size_t end = start + 1;
                while (end < json.length() && brace_count > 0) {
                    if (json[end] == '{') brace_count++;
                    else if (json[end] == '}') brace_count--;
                    end++;
                }
                if (brace_count == 0) {
                    message.data = json.substr(start, end - start);
                }
            }
        }
    }

    
    return message;
}

// WebSocketFrame implementation
std::vector<uint8_t> WebSocketFrame::serialize() const {
    std::vector<uint8_t> frame;
    
    // First byte: FIN + RSV + Opcode
    uint8_t first_byte = (fin ? 0x80 : 0x00) | static_cast<uint8_t>(opcode);
    frame.push_back(first_byte);
    
    // Second byte: MASK + Payload length
    uint8_t second_byte = masked ? 0x80 : 0x00;
    
    if (payload.size() < 126) {
        second_byte |= static_cast<uint8_t>(payload.size());
        frame.push_back(second_byte);
    } else if (payload.size() < 65536) {
        second_byte |= 126;
        frame.push_back(second_byte);
        frame.push_back((payload.size() >> 8) & 0xFF);
        frame.push_back(payload.size() & 0xFF);
    } else {
        second_byte |= 127;
        frame.push_back(second_byte);
        // 64-bit length (simplified - only use lower 32 bits)
        frame.push_back(0); frame.push_back(0); frame.push_back(0); frame.push_back(0);
        frame.push_back((payload.size() >> 24) & 0xFF);
        frame.push_back((payload.size() >> 16) & 0xFF);
        frame.push_back((payload.size() >> 8) & 0xFF);
        frame.push_back(payload.size() & 0xFF);
    }
    
    // Masking key (if masked)
    if (masked) {
        frame.push_back((mask >> 24) & 0xFF);
        frame.push_back((mask >> 16) & 0xFF);
        frame.push_back((mask >> 8) & 0xFF);
        frame.push_back(mask & 0xFF);
    }
    
    // Payload (apply mask if needed)
    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i) {
            uint8_t mask_byte = (mask >> (8 * (3 - (i % 4)))) & 0xFF;
            frame.push_back(payload[i] ^ mask_byte);
        }
    } else {
        frame.insert(frame.end(), payload.begin(), payload.end());
    }
    
    return frame;
}

WebSocketFrame WebSocketFrame::deserialize(const uint8_t* data, size_t size) {
    WebSocketFrame frame;
    
    if (size < 2) {
        return frame; // Invalid frame
    }
    
    // Parse first byte
    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = static_cast<WebSocketOpcode>(data[0] & 0x0F);
    
    // Parse second byte
    frame.masked = (data[1] & 0x80) != 0;
    uint8_t payload_len = data[1] & 0x7F;
    
    size_t header_size = 2;
    size_t payload_size = payload_len;
    
    // Extended payload length
    if (payload_len == 126) {
        if (size < 4) return frame;
        payload_size = (data[2] << 8) | data[3];
        header_size = 4;
    } else if (payload_len == 127) {
        if (size < 10) return frame;
        // Simplified - only use lower 32 bits
        payload_size = (data[6] << 24) | (data[7] << 16) | (data[8] << 8) | data[9];
        header_size = 10;
    }
    
    // Masking key
    if (frame.masked) {
        if (size < header_size + 4) return frame;
        frame.mask = (data[header_size] << 24) | (data[header_size + 1] << 16) |
                    (data[header_size + 2] << 8) | data[header_size + 3];
        header_size += 4;
    }
    
    // Payload
    if (size < header_size + payload_size) return frame;
    
    frame.payload.resize(payload_size);
    if (frame.masked) {
        for (size_t i = 0; i < payload_size; ++i) {
            uint8_t mask_byte = (frame.mask >> (8 * (3 - (i % 4)))) & 0xFF;
            frame.payload[i] = data[header_size + i] ^ mask_byte;
        }
    } else {
        std::memcpy(frame.payload.data(), data + header_size, payload_size);
    }
    
    return frame;
}

bool WebSocketFrame::isWebSocketFrame(const uint8_t* data, size_t size) {
    if (size < 2) return false;
    
    // Check if it looks like a WebSocket frame
    uint8_t opcode = data[0] & 0x0F;
    return opcode <= 0x0A; // Valid opcodes are 0x0-0x2 and 0x8-0xA
}

// WebSocketConnection implementation
WebSocketConnection::WebSocketConnection(std::shared_ptr<network::TcpSocket> socket)
    : socket_(socket), connected_(false), handshake_complete_(false) {
    
    connection_id_ = generateClientId();
    
    socket_->setDataCallback([this](const std::vector<uint8_t>& data, const network::SocketAddress& from) {
        onSocketData(data, from);
    });
    
    socket_->setErrorCallback([this](const std::string& error) {
        onSocketError(error);
    });
    
    socket_->startReceiving();
}

WebSocketConnection::~WebSocketConnection() {
    close();
}

bool WebSocketConnection::performHandshake(const std::string& request) {
    // Parse WebSocket handshake request
    std::regex key_regex(R"(Sec-WebSocket-Key:\s*([^\r\n]+))");
    std::smatch match;
    
    if (!std::regex_search(request, match, key_regex)) {
        return false;
    }
    
    std::string websocket_key = match[1].str();
    std::string websocket_accept = generateWebSocketAccept(websocket_key);
    
    // Create handshake response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << websocket_accept << "\r\n"
             << "\r\n";
    
    std::string response_str = response.str();
    std::vector<uint8_t> response_data(response_str.begin(), response_str.end());
    
    if (socket_->send(response_data)) {
        handshake_complete_ = true;
        connected_ = true;
        core::Logger::info("WebSocket handshake completed for connection {}", connection_id_);
        return true;
    }
    
    return false;
}

void WebSocketConnection::close() {
    if (connected_) {
        // Send close frame
        WebSocketFrame close_frame;
        close_frame.opcode = WebSocketOpcode::CLOSE;
        close_frame.fin = true;
        
        auto frame_data = close_frame.serialize();
        socket_->send(frame_data);
        
        connected_ = false;
        
        if (close_callback_) {
            close_callback_();
        }
    }
    
    if (socket_) {
        socket_->close();
    }
}

bool WebSocketConnection::sendMessage(const std::string& message) {
    if (!connected_) {
        return false;
    }
    
    WebSocketFrame frame;
    frame.opcode = WebSocketOpcode::TEXT;
    frame.fin = true;
    frame.payload.assign(message.begin(), message.end());
    
    auto frame_data = frame.serialize();
    return socket_->send(frame_data);
}

bool WebSocketConnection::sendPing() {
    if (!connected_) {
        return false;
    }
    
    WebSocketFrame frame;
    frame.opcode = WebSocketOpcode::PING;
    frame.fin = true;
    
    auto frame_data = frame.serialize();
    return socket_->send(frame_data);
}

bool WebSocketConnection::sendPong() {
    if (!connected_) {
        return false;
    }
    
    WebSocketFrame frame;
    frame.opcode = WebSocketOpcode::PONG;
    frame.fin = true;
    
    auto frame_data = frame.serialize();
    return socket_->send(frame_data);
}

const network::SocketAddress& WebSocketConnection::getRemoteAddress() const {
    static network::SocketAddress default_addr;
    if (socket_) {
        return socket_->getRemoteAddress();
    }
    return default_addr;
}

void WebSocketConnection::onSocketData(const std::vector<uint8_t>& data, const network::SocketAddress& /* from */) {
    if (!handshake_complete_) {
        // Handle HTTP handshake
        std::string request(data.begin(), data.end());
        if (request.find("GET") == 0 && request.find("Upgrade: websocket") != std::string::npos) {
            performHandshake(request);
        }
        return;
    }
    
    // Append to frame buffer
    frame_buffer_.insert(frame_buffer_.end(), data.begin(), data.end());
    
    // Try to parse WebSocket frames
    while (frame_buffer_.size() >= 2) {
        if (!WebSocketFrame::isWebSocketFrame(frame_buffer_.data(), frame_buffer_.size())) {
            frame_buffer_.clear();
            break;
        }
        
        WebSocketFrame frame = WebSocketFrame::deserialize(frame_buffer_.data(), frame_buffer_.size());
        
        // Check if we have a complete frame
        size_t frame_size = 2; // Minimum header size
        uint8_t payload_len = frame_buffer_[1] & 0x7F;
        
        if (payload_len == 126) frame_size += 2;
        else if (payload_len == 127) frame_size += 8;
        
        if (frame_buffer_[1] & 0x80) frame_size += 4; // Mask
        
        size_t actual_payload_size = payload_len;
        if (payload_len == 126 && frame_buffer_.size() >= 4) {
            actual_payload_size = (frame_buffer_[2] << 8) | frame_buffer_[3];
        } else if (payload_len == 127 && frame_buffer_.size() >= 10) {
            actual_payload_size = (frame_buffer_[6] << 24) | (frame_buffer_[7] << 16) |
                                 (frame_buffer_[8] << 8) | frame_buffer_[9];
        }
        
        frame_size += actual_payload_size;
        
        if (frame_buffer_.size() < frame_size) {
            break; // Wait for more data
        }
        
        processWebSocketFrame(frame);
        
        // Remove processed frame from buffer
        frame_buffer_.erase(frame_buffer_.begin(), frame_buffer_.begin() + frame_size);
    }
}

void WebSocketConnection::onSocketError(const std::string& error) {
    core::Logger::error("WebSocket connection {} error: {}", connection_id_, error);
    connected_ = false;
    
    if (error_callback_) {
        error_callback_(error);
    }
}

void WebSocketConnection::processWebSocketFrame(const WebSocketFrame& frame) {
    switch (frame.opcode) {
        case WebSocketOpcode::TEXT: {
            std::string message(frame.payload.begin(), frame.payload.end());
            if (message_callback_) {
                message_callback_(message);
            }
            break;
        }
        
        case WebSocketOpcode::PING:
            sendPong();
            break;
            
        case WebSocketOpcode::PONG:
            // Handle pong if needed
            break;
            
        case WebSocketOpcode::CLOSE:
            close();
            break;
            
        default:
            core::Logger::debug("Unhandled WebSocket opcode: {}", static_cast<int>(frame.opcode));
            break;
    }
}

std::string WebSocketConnection::generateWebSocketAccept(const std::string& key) const {
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = key + magic;
    
    // Calculate SHA-1 hash (simplified implementation)
    std::string hash = calculateSHA1(combined);
    
    // Base64 encode
    std::vector<uint8_t> hash_bytes(hash.begin(), hash.end());
    return base64Encode(hash_bytes);
}

std::string WebSocketConnection::calculateSHA1(const std::string& data) const {
    // Placeholder implementation - would use proper SHA-1 library in production
    std::hash<std::string> hasher;
    size_t hash_value = hasher(data);
    
    std::ostringstream oss;
    oss << std::hex << hash_value;
    return oss.str();
}

std::string WebSocketConnection::base64Encode(const std::vector<uint8_t>& data) const {
    // Simplified base64 encoding
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t value = 0;
        int count = 0;
        
        for (int j = 0; j < 3 && i + j < data.size(); ++j) {
            value = (value << 8) | data[i + j];
            count++;
        }
        
        value <<= (3 - count) * 8;
        
        for (int j = 0; j < 4; ++j) {
            if (j <= count) {
                result += chars[(value >> (18 - j * 6)) & 0x3F];
            } else {
                result += '=';
            }
        }
    }
    
    return result;
}

// SignalingServer implementation
SignalingServer::SignalingServer() : running_(false) {
}

SignalingServer::~SignalingServer() {
    stop();
}

bool SignalingServer::start(const network::SocketAddress& bind_address) {
    if (running_) {
        return true;
    }

    server_socket_ = network::createTcpSocket();
    if (!server_socket_) {
        return false;
    }

    server_socket_->setConnectionCallback([this](std::shared_ptr<network::Socket> connection) {
        onNewConnection(connection);
    });

    server_socket_->setErrorCallback([this](const std::string& error) {
        core::Logger::error("Signaling server error: {}", error);
        stats_.errors++;
    });

    if (!server_socket_->bind(bind_address)) {
        server_socket_.reset();
        return false;
    }

    if (!server_socket_->listen()) {
        server_socket_.reset();
        return false;
    }

    server_socket_->acceptConnections();
    running_ = true;

    core::Logger::info("WebRTC signaling server started on {}", bind_address.toString());
    return true;
}

void SignalingServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // Close all connections
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, connection] : connections_) {
        connection->close();
    }
    connections_.clear();
    sessions_.clear();
    client_to_session_.clear();

    if (server_socket_) {
        server_socket_->close();
        server_socket_.reset();
    }

    core::Logger::info("WebRTC signaling server stopped");
}

bool SignalingServer::sendMessage(const SignalingMessage& message, const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        bool success = it->second->sendMessage(message.toJson());
        if (success) {
            stats_.messages_sent++;
        } else {
            stats_.errors++;
        }
        return success;
    }

    return false;
}

bool SignalingServer::broadcastMessage(const SignalingMessage& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool success = true;
    for (auto& [id, connection] : connections_) {
        if (!connection->sendMessage(message.toJson())) {
            success = false;
            stats_.errors++;
        } else {
            stats_.messages_sent++;
        }
    }

    return success;
}

bool SignalingServer::sendToSession(const SignalingMessage& message, const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        return false;
    }

    bool success = true;
    for (const std::string& participant_id : session_it->second.participants) {
        auto conn_it = connections_.find(participant_id);
        if (conn_it != connections_.end()) {
            if (!conn_it->second->sendMessage(message.toJson())) {
                success = false;
                stats_.errors++;
            } else {
                stats_.messages_sent++;
            }
        }
    }

    return success;
}

void SignalingServer::disconnectClient(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        it->second->close();
        connections_.erase(it);

        // Remove from session
        auto session_it = client_to_session_.find(connection_id);
        if (session_it != client_to_session_.end()) {
            leaveSession(session_it->second, connection_id);
        }

        stats_.connections_closed++;
    }
}

std::vector<std::string> SignalingServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> clients;
    for (const auto& [id, connection] : connections_) {
        clients.push_back(id);
    }

    return clients;
}

size_t SignalingServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

void SignalingServer::createSession(const std::string& session_id, const std::string& creator_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    Session session;
    session.id = session_id;
    session.creator_id = creator_id;
    session.participants.push_back(creator_id);
    session.created = std::chrono::system_clock::now();

    sessions_[session_id] = session;
    client_to_session_[creator_id] = session_id;

    stats_.sessions_created++;
    core::Logger::info("Created session {} with creator {}", session_id, creator_id);
}

void SignalingServer::joinSession(const std::string& session_id, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        // Remove from previous session if any
        auto prev_session_it = client_to_session_.find(client_id);
        if (prev_session_it != client_to_session_.end()) {
            leaveSession(prev_session_it->second, client_id);
        }

        it->second.participants.push_back(client_id);
        client_to_session_[client_id] = session_id;

        core::Logger::info("Client {} joined session {}", client_id, session_id);
    }
}

void SignalingServer::leaveSession(const std::string& session_id, const std::string& client_id) {
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        auto& participants = it->second.participants;
        participants.erase(std::remove(participants.begin(), participants.end(), client_id), participants.end());

        client_to_session_.erase(client_id);

        // Destroy session if empty
        if (participants.empty()) {
            sessions_.erase(it);
            stats_.sessions_destroyed++;
            core::Logger::info("Destroyed empty session {}", session_id);
        } else {
            core::Logger::info("Client {} left session {}", client_id, session_id);
        }
    }
}

void SignalingServer::destroySession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        // Remove all participants from session mapping
        for (const std::string& participant : it->second.participants) {
            client_to_session_.erase(participant);
        }

        sessions_.erase(it);
        stats_.sessions_destroyed++;
        core::Logger::info("Destroyed session {}", session_id);
    }
}

std::vector<std::string> SignalingServer::getSessionParticipants(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        return it->second.participants;
    }

    return {};
}

std::vector<std::string> SignalingServer::getActiveSessions() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> session_ids;
    for (const auto& [id, session] : sessions_) {
        session_ids.push_back(id);
    }

    return session_ids;
}

void SignalingServer::onNewConnection(std::shared_ptr<network::Socket> connection) {
    auto tcp_connection = std::dynamic_pointer_cast<network::TcpSocket>(connection);
    if (!tcp_connection) {
        return;
    }

    auto ws_connection = std::make_shared<WebSocketConnection>(tcp_connection);

    ws_connection->setMessageCallback([this, ws_connection](const std::string& message) {
        onConnectionMessage(message, ws_connection->getId());
    });

    ws_connection->setCloseCallback([this, ws_connection]() {
        onConnectionClosed(ws_connection->getId());
    });

    ws_connection->setErrorCallback([this, ws_connection](const std::string& error) {
        onConnectionError(error, ws_connection->getId());
    });

    std::lock_guard<std::mutex> lock(mutex_);
    connections_[ws_connection->getId()] = ws_connection;
    stats_.connections_accepted++;

    if (client_connected_callback_) {
        client_connected_callback_(ws_connection->getId());
    }

    core::Logger::info("New WebSocket connection: {}", ws_connection->getId());
}

void SignalingServer::onConnectionMessage(const std::string& message, const std::string& connection_id) {
    try {
        SignalingMessage sig_message = SignalingMessage::fromJson(message);
        if (sig_message.isValid()) {
            processSignalingMessage(sig_message, connection_id);
            stats_.messages_received++;
        } else {
            core::Logger::warn("Invalid signaling message from {}: {}", connection_id, message);
            stats_.errors++;
        }
    } catch (const std::exception& e) {
        core::Logger::error("Error processing message from {}: {}", connection_id, e.what());
        stats_.errors++;
    }
}

void SignalingServer::onConnectionClosed(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    connections_.erase(connection_id);

    // Remove from session
    auto session_it = client_to_session_.find(connection_id);
    if (session_it != client_to_session_.end()) {
        leaveSession(session_it->second, connection_id);
    }

    stats_.connections_closed++;

    if (client_disconnected_callback_) {
        client_disconnected_callback_(connection_id);
    }

    core::Logger::info("WebSocket connection closed: {}", connection_id);
}

void SignalingServer::onConnectionError(const std::string& error, const std::string& connection_id) {
    core::Logger::error("WebSocket connection {} error: {}", connection_id, error);
    stats_.errors++;
}

void SignalingServer::processSignalingMessage(const SignalingMessage& message, const std::string& connection_id) {
    switch (message.type) {
        case SignalingMessageType::OFFER:
            handleOffer(message, connection_id);
            break;
        case SignalingMessageType::ANSWER:
            handleAnswer(message, connection_id);
            break;
        case SignalingMessageType::ICE_CANDIDATE:
            handleIceCandidate(message, connection_id);
            break;
        case SignalingMessageType::BYE:
            handleBye(message, connection_id);
            break;
        case SignalingMessageType::PING:
            // Send pong response
            sendMessage(SignalingMessage(SignalingMessageType::PONG, message.session_id, "server", message.from, ""), connection_id);
            break;
        default:
            core::Logger::debug("Unhandled signaling message type: {}", static_cast<int>(message.type));
            break;
    }

    if (message_callback_) {
        message_callback_(message, connection_id);
    }
}

void SignalingServer::handleOffer(const SignalingMessage& message, const std::string& connection_id) {
    if (!message.to.empty()) {
        // Direct message to specific client
        sendMessage(message, message.to);
    } else {
        // Broadcast to session
        sendToSession(message, message.session_id);
    }

    core::Logger::info("Relayed offer from {} in session {}", connection_id, message.session_id);
}

void SignalingServer::handleAnswer(const SignalingMessage& message, const std::string& connection_id) {
    if (!message.to.empty()) {
        // Direct message to specific client
        sendMessage(message, message.to);
    } else {
        // Broadcast to session
        sendToSession(message, message.session_id);
    }

    core::Logger::info("Relayed answer from {} in session {}", connection_id, message.session_id);
}

void SignalingServer::handleIceCandidate(const SignalingMessage& message, const std::string& connection_id) {
    if (!message.to.empty()) {
        // Direct message to specific client
        sendMessage(message, message.to);
    } else {
        // Broadcast to session
        sendToSession(message, message.session_id);
    }

    core::Logger::debug("Relayed ICE candidate from {} in session {}", connection_id, message.session_id);
}

void SignalingServer::handleBye(const SignalingMessage& message, const std::string& connection_id) {
    // Remove client from session
    auto session_it = client_to_session_.find(connection_id);
    if (session_it != client_to_session_.end()) {
        leaveSession(session_it->second, connection_id);
    }

    // Notify other participants
    if (!message.to.empty()) {
        sendMessage(message, message.to);
    } else {
        sendToSession(message, message.session_id);
    }

    core::Logger::info("Client {} left session {}", connection_id, message.session_id);
}

// Utility functions
std::string generateSessionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "session-";

    for (int i = 0; i < 16; ++i) {
        oss << std::hex << dis(gen);
    }

    return oss.str();
}

std::string generateClientId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "client-";

    for (int i = 0; i < 12; ++i) {
        oss << std::hex << dis(gen);
    }

    return oss.str();
}

std::string signalingMessageTypeToString(SignalingMessageType type) {
    switch (type) {
        case SignalingMessageType::OFFER: return "offer";
        case SignalingMessageType::ANSWER: return "answer";
        case SignalingMessageType::ICE_CANDIDATE: return "ice-candidate";
        case SignalingMessageType::BYE: return "bye";
        case SignalingMessageType::ERROR: return "error";
        case SignalingMessageType::PING: return "ping";
        case SignalingMessageType::PONG: return "pong";
        default: return "unknown";
    }
}

SignalingMessageType stringToSignalingMessageType(const std::string& type) {
    if (type == "offer") return SignalingMessageType::OFFER;
    if (type == "answer") return SignalingMessageType::ANSWER;
    if (type == "ice-candidate") return SignalingMessageType::ICE_CANDIDATE;
    if (type == "bye") return SignalingMessageType::BYE;
    if (type == "error") return SignalingMessageType::ERROR;
    if (type == "ping") return SignalingMessageType::PING;
    if (type == "pong") return SignalingMessageType::PONG;
    return SignalingMessageType::ERROR;
}

} // namespace fmus::webrtc
