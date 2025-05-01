#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>
#include <fmus/network/network.hpp>

#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>

namespace fmus::webrtc {

// WebRTC signaling implementation
class WebRtcSignalingImpl : public WebRtcSignaling,
                          public std::enable_shared_from_this<WebRtcSignalingImpl> {
public:
    WebRtcSignalingImpl()
        : state_(WebRtcSignalingState::Disconnected) {
        core::Logger::info("WebRtcSignalingImpl created");
    }

    ~WebRtcSignalingImpl() {
        disconnect();
        core::Logger::info("WebRtcSignalingImpl destroyed");
    }

    // Connect/disconnect to signaling server
    core::Task<void> connect(const std::string& server_url) override {
        // Memeriksa status koneksi
        if (state_ != WebRtcSignalingState::Disconnected) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Signaling channel is already connecting or connected");
        }

        server_url_ = server_url;

        // Mengubah status
        setState(WebRtcSignalingState::Connecting);

        // Mensimulasikan proses koneksi
        core::Logger::info("Connecting to signaling server: {}", server_url);

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(500));

        // Connect berhasil
        setState(WebRtcSignalingState::Connected);

        co_return;
    }

    void disconnect() override {
        if (state_ == WebRtcSignalingState::Disconnected) {
            return;
        }

        core::Logger::info("Disconnecting from signaling server");

        // Leave current room if any
        if (!current_room_.empty()) {
            leave();
        }

        // Reset state
        setState(WebRtcSignalingState::Disconnected);
        server_url_.clear();
    }

    // Join/leave room
    core::Task<std::vector<std::string>> join(const std::string& room_id, const std::string& client_id) override {
        // Memeriksa status koneksi
        if (state_ != WebRtcSignalingState::Connected) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Signaling channel is not connected");
        }

        // Leave current room if needed
        if (!current_room_.empty()) {
            leave();
        }

        core::Logger::info("Joining room: {}, client ID: {}", room_id, client_id);

        // Mengatur status
        current_room_ = room_id;
        client_id_ = client_id;

        // Simulasi dummy clients already in the room
        std::vector<std::string> peers = {"peer1", "peer2"};

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(200));

        // Update peer list
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_in_room_.clear();
            for (const auto& peer : peers) {
                clients_in_room_.insert(peer);

                // Memicu event clientJoined
                onClientJoined.emit(peer);
            }
        }

        co_return peers;
    }

    void leave() override {
        if (current_room_.empty()) {
            return;
        }

        core::Logger::info("Leaving room: {}", current_room_);

        // Cleanup
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_in_room_.clear();
        }

        current_room_.clear();
    }

    // Send message
    core::Task<void> sendMessage(const WebRtcSignalingMessage& message) override {
        // Memeriksa status koneksi
        if (state_ != WebRtcSignalingState::Connected) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Signaling channel is not connected");
        }

        // Memeriksa room
        if (current_room_.empty()) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Not in a room");
        }

        core::Logger::info("Sending message: type={}, target={}",
                         static_cast<int>(message.type), message.target_id);

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(100));

        // Untuk simulasi, kita anggap message selalu sampai ke target
        // Dalam implementasi nyata, message akan dikirim ke server signaling

        co_return;
    }

    // State
    WebRtcSignalingState state() const override {
        return state_;
    }

    std::string currentRoom() const override {
        return current_room_;
    }

    std::string clientId() const override {
        return client_id_;
    }

private:
    // Helper methods
    void setState(WebRtcSignalingState new_state) {
        if (state_ != new_state) {
            state_ = new_state;
            onStateChange.emit(state_);
        }
    }

    // Simulate receiving a message
    void simulateReceiveMessage(const WebRtcSignalingMessage& message) {
        onMessage.emit(message);
    }

    // Simulate peer join/leave
    void simulatePeerJoin(const std::string& peer_id) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_in_room_.insert(peer_id);
        }
        onClientJoined.emit(peer_id);
    }

    void simulatePeerLeave(const std::string& peer_id) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_in_room_.erase(peer_id);
        }
        onClientLeft.emit(peer_id);
    }

    // State
    std::atomic<WebRtcSignalingState> state_ = WebRtcSignalingState::Disconnected;
    std::string server_url_;
    std::string current_room_;
    std::string client_id_;

    // Clients in room
    std::set<std::string> clients_in_room_;

    // Mutex for thread safety
    mutable std::mutex mutex_;
};

// Factory method implementation
std::unique_ptr<WebRtcSignaling> createSignalingImpl() {
    return std::make_unique<WebRtcSignalingImpl>();
}

} // namespace fmus::webrtc