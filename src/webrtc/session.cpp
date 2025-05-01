#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>

namespace fmus::webrtc {

// WebRTC session implementation
class WebRtcSessionImpl : public WebRtcSession,
                        public std::enable_shared_from_this<WebRtcSessionImpl> {
public:
    WebRtcSessionImpl(std::shared_ptr<WebRtcSignaling> signaling,
                     const WebRtcConfiguration& config)
        : signaling_(signaling), config_(config) {
        core::Logger::info("WebRtcSessionImpl created");

        // Set up signaling event handlers
        if (signaling_) {
            setupSignalingEvents();
        }
    }

    ~WebRtcSessionImpl() {
        stop();
        core::Logger::info("WebRtcSessionImpl destroyed");
    }

    // Session management
    core::Task<void> start() override {
        if (is_running_) {
            co_return;
        }

        core::Logger::info("Starting WebRTC session");

        is_running_ = true;

        // Make sure signaling is connected
        if (signaling_ && signaling_->state() != WebRtcSignalingState::Connected) {
            try {
                co_await signaling_->connect("wss://example.com/signaling");
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to connect to signaling server: {}", e.what());
                onError.emit(e);
                stop();
                co_return;
            }
        }

        co_return;
    }

    void stop() override {
        if (!is_running_) {
            return;
        }

        core::Logger::info("Stopping WebRTC session");

        // Disconnect all peers
        disconnectAll();

        // Clear local streams
        local_streams_.clear();

        is_running_ = false;
    }

    // Peer management
    core::Task<void> connect(const std::string& peer_id) override {
        if (!is_running_) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Session is not running");
        }

        // Memeriksa apakah peer_id sudah ada
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (peer_connections_.find(peer_id) != peer_connections_.end()) {
                core::Logger::warn("Already connected to peer: {}", peer_id);
                co_return;
            }
        }

        core::Logger::info("Connecting to peer: {}", peer_id);

        // Membuat peer connection baru
        auto pc = WebRtcPeerConnection::create(config_);

        // Setup event handlers untuk peer connection
        setupPeerConnectionEvents(pc, peer_id);

        // Menambahkan semua local tracks ke peer connection
        for (const auto& stream : local_streams_) {
            for (const auto& track : stream->getTracks()) {
                try {
                    pc->addTrack(track, {stream});
                } catch (const WebRtcError& e) {
                    core::Logger::warn("Failed to add track to peer connection: {}", e.what());
                }
            }
        }

        // Simpan peer connection
        {
            std::lock_guard<std::mutex> lock(mutex_);
            peer_connections_[peer_id] = pc;
        }

        // Create offer dan set local description
        try {
            auto offer = co_await pc->createOffer();
            co_await pc->setLocalDescription(offer);

            // Mengirim offer ke peer melalui signaling
            WebRtcSignalingMessage message;
            message.type = WebRtcSignalingMessageType::Offer;
            message.sender_id = signaling_->clientId();
            message.target_id = peer_id;
            message.content = offer.toJson();

            co_await signaling_->sendMessage(message);
        } catch (const WebRtcError& e) {
            core::Logger::error("Failed to create and send offer: {}", e.what());

            // Hapus peer connection
            std::lock_guard<std::mutex> lock(mutex_);
            peer_connections_.erase(peer_id);

            // Propagate error
            onError.emit(e);
            co_return;
        }

        co_return;
    }

    void disconnect(const std::string& peer_id) override {
        core::Logger::info("Disconnecting from peer: {}", peer_id);

        std::shared_ptr<WebRtcPeerConnection> pc;

        // Get and remove peer connection
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = peer_connections_.find(peer_id);
            if (it == peer_connections_.end()) {
                return;
            }

            pc = it->second;
            peer_connections_.erase(it);
        }

        // Send hangup message
        if (signaling_ && signaling_->state() == WebRtcSignalingState::Connected) {
            WebRtcSignalingMessage message;
            message.type = WebRtcSignalingMessageType::Hangup;
            message.sender_id = signaling_->clientId();
            message.target_id = peer_id;

            // Kirim hangup message asynchronously
            signaling_->sendMessage(message).then([](auto) {}, [](auto) {});
        }

        // Close peer connection
        if (pc) {
            pc->close();
        }

        // Remove remote streams
        {
            std::lock_guard<std::mutex> lock(mutex_);
            remote_streams_.erase(peer_id);
        }

        // Emit event
        onPeerDisconnected.emit(peer_id);
    }

    void disconnectAll() override {
        core::Logger::info("Disconnecting from all peers");

        std::map<std::string, std::shared_ptr<WebRtcPeerConnection>> pcs;

        // Get all peer connections
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pcs = peer_connections_;
            peer_connections_.clear();
            remote_streams_.clear();
        }

        // Send hangup messages and close peer connections
        for (const auto& [peer_id, pc] : pcs) {
            if (signaling_ && signaling_->state() == WebRtcSignalingState::Connected) {
                WebRtcSignalingMessage message;
                message.type = WebRtcSignalingMessageType::Hangup;
                message.sender_id = signaling_->clientId();
                message.target_id = peer_id;

                // Kirim hangup message asynchronously
                signaling_->sendMessage(message).then([](auto) {}, [](auto) {});
            }

            if (pc) {
                pc->close();
            }

            // Emit event
            onPeerDisconnected.emit(peer_id);
        }
    }

    std::set<std::string> getConnectedPeers() const override {
        std::set<std::string> result;

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [peer_id, _] : peer_connections_) {
            result.insert(peer_id);
        }

        return result;
    }

    std::shared_ptr<WebRtcPeerConnection> getPeerConnection(const std::string& peer_id) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = peer_connections_.find(peer_id);
        if (it != peer_connections_.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Media management
    core::Task<void> addLocalStream(std::shared_ptr<media::MediaStream> stream) override {
        if (!stream) {
            throw WebRtcError(WebRtcErrorCode::InvalidParameter, "Stream is null");
        }

        // Tambahkan stream ke local_streams_
        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Periksa apakah stream sudah ada
            for (const auto& s : local_streams_) {
                if (s == stream) {
                    co_return;  // Stream already added
                }
            }

            local_streams_.push_back(stream);
        }

        core::Logger::info("Added local stream with {} tracks", stream->getTracks().size());

        // Tambahkan semua tracks dari stream ke semua peer connections
        std::map<std::string, std::shared_ptr<WebRtcPeerConnection>> pcs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pcs = peer_connections_;
        }

        for (const auto& [peer_id, pc] : pcs) {
            for (const auto& track : stream->getTracks()) {
                try {
                    pc->addTrack(track, {stream});
                } catch (const WebRtcError& e) {
                    core::Logger::warn("Failed to add track to peer {}: {}", peer_id, e.what());
                }
            }
        }

        co_return;
    }

    void removeLocalStream(std::shared_ptr<media::MediaStream> stream) override {
        if (!stream) {
            return;
        }

        // Remove stream from local_streams_
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = std::find(local_streams_.begin(), local_streams_.end(), stream);
            if (it == local_streams_.end()) {
                return;  // Stream not found
            }

            local_streams_.erase(it);
        }

        core::Logger::info("Removed local stream");

        // Remove all tracks from the stream from all peer connections
        std::map<std::string, std::shared_ptr<WebRtcPeerConnection>> pcs;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pcs = peer_connections_;
        }

        for (const auto& [peer_id, pc] : pcs) {
            auto senders = pc->getSenders();
            for (const auto& sender : senders) {
                auto track = sender->track();
                if (track) {
                    // Check if this track is from the stream being removed
                    bool found = false;
                    for (const auto& stream_track : stream->getTracks()) {
                        if (track->id() == stream_track->id()) {
                            found = true;
                            break;
                        }
                    }

                    if (found) {
                        try {
                            pc->removeTrack(sender);
                        } catch (const WebRtcError& e) {
                            core::Logger::warn("Failed to remove track from peer {}: {}",
                                             peer_id, e.what());
                        }
                    }
                }
            }
        }
    }

    std::vector<std::shared_ptr<media::MediaStream>> getLocalStreams() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return local_streams_;
    }

    std::map<std::string, std::vector<std::shared_ptr<media::MediaStream>>> getRemoteStreams() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return remote_streams_;
    }

    // Data channels
    std::shared_ptr<WebRtcDataChannel> createDataChannel(
        const std::string& peer_id,
        const std::string& label,
        const WebRtcDataChannelInit& options) override {

        // Get peer connection
        auto pc = getPeerConnection(peer_id);
        if (!pc) {
            throw WebRtcError(WebRtcErrorCode::InvalidParameter,
                            "Peer connection not found: " + peer_id);
        }

        // Create data channel
        return pc->createDataChannel(label, options);
    }

private:
    // Set up signaling event handlers
    void setupSignalingEvents() {
        if (!signaling_) {
            return;
        }

        // Handle state changes
        signaling_->onStateChange.on([this](WebRtcSignalingState state) {
            if (state == WebRtcSignalingState::Disconnected && is_running_) {
                // Disconnect all peers if signaling disconnects
                disconnectAll();
            }
        });

        // Handle incoming messages
        signaling_->onMessage.on([this](const WebRtcSignalingMessage& message) {
            handleSignalingMessage(message);
        });

        // Handle client join/leave
        signaling_->onClientJoined.on([this](const std::string& client_id) {
            // Do nothing, let the application decide whether to connect
        });

        signaling_->onClientLeft.on([this](const std::string& client_id) {
            // Disconnect the peer that left
            disconnect(client_id);
        });

        // Handle errors
        signaling_->onError.on([this](const WebRtcError& error) {
            // Propagate error
            onError.emit(error);
        });
    }

    // Set up peer connection event handlers
    void setupPeerConnectionEvents(std::shared_ptr<WebRtcPeerConnection> pc,
                                  const std::string& peer_id) {
        if (!pc) {
            return;
        }

        // Handle ICE candidates
        pc->onIceCandidate.on([this, peer_id](const WebRtcIceCandidate& candidate) {
            // Send the ICE candidate to the peer via signaling
            if (signaling_ && signaling_->state() == WebRtcSignalingState::Connected) {
                WebRtcSignalingMessage message;
                message.type = WebRtcSignalingMessageType::IceCandidate;
                message.sender_id = signaling_->clientId();
                message.target_id = peer_id;
                message.content = candidate.toJson();

                // Kirim message asynchronously
                signaling_->sendMessage(message).then([](auto) {}, [](auto) {});
            }
        });

        // Handle connection state changes
        pc->onConnectionStateChange.on([this, peer_id](WebRtcPeerConnectionState state) {
            if (state == WebRtcPeerConnectionState::Connected) {
                // Emit peer connected event
                onPeerConnected.emit(peer_id);
            } else if (state == WebRtcPeerConnectionState::Failed ||
                       state == WebRtcPeerConnectionState::Closed) {
                // Disconnect peer on failure or close
                disconnect(peer_id);
            }
        });

        // Handle negotiation needed
        pc->onNegotiationNeeded.on([this, peer_id, pc]() {
            // Create and send a new offer
            auto task = [this, peer_id, pc]() -> core::Task<void> {
                try {
                    auto offer = co_await pc->createOffer();
                    co_await pc->setLocalDescription(offer);

                    // Send offer to peer
                    if (signaling_ && signaling_->state() == WebRtcSignalingState::Connected) {
                        WebRtcSignalingMessage message;
                        message.type = WebRtcSignalingMessageType::Offer;
                        message.sender_id = signaling_->clientId();
                        message.target_id = peer_id;
                        message.content = offer.toJson();

                        co_await signaling_->sendMessage(message);
                    }
                } catch (const WebRtcError& e) {
                    core::Logger::error("Negotiation failed: {}", e.what());
                    onError.emit(e);
                }
            };

            // Execute task asynchronously
            task();
        });

        // Handle tracks
        pc->onTrack.on([this, peer_id](const WebRtcTrackEvent& event) {
            // Add track to remote streams
            if (event.track) {
                core::Logger::info("Received track: {} from peer: {}",
                                event.track->id(), peer_id);

                // Add track to remote streams
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    // If no streams provided, create a default one
                    if (event.streams.empty()) {
                        // Would create a default stream in a real implementation
                    } else {
                        for (const auto& stream : event.streams) {
                            remote_streams_[peer_id].push_back(stream);
                        }
                    }
                }

                // Emit track event
                onRemoteTrack.emit(std::make_pair(peer_id, event));
            }
        });

        // Handle data channels
        pc->onDataChannel.on([this, peer_id](std::shared_ptr<WebRtcDataChannel> channel) {
            // Emit data channel event
            onDataChannel.emit(std::make_pair(peer_id, channel));
        });

        // Handle errors
        pc->onError.on([this](const WebRtcError& error) {
            // Propagate error
            onError.emit(error);
        });
    }

    // Handle signaling messages
    void handleSignalingMessage(const WebRtcSignalingMessage& message) {
        // Skip messages from self
        if (message.sender_id == signaling_->clientId()) {
            return;
        }

        core::Logger::info("Received signaling message: type={}, sender={}",
                        static_cast<int>(message.type), message.sender_id);

        switch (message.type) {
            case WebRtcSignalingMessageType::Offer:
                handleOffer(message.sender_id, message.content);
                break;

            case WebRtcSignalingMessageType::Answer:
                handleAnswer(message.sender_id, message.content);
                break;

            case WebRtcSignalingMessageType::IceCandidate:
                handleIceCandidate(message.sender_id, message.content);
                break;

            case WebRtcSignalingMessageType::Hangup:
                handleHangup(message.sender_id);
                break;

            case WebRtcSignalingMessageType::Error:
                handleError(message.sender_id, message.content);
                break;
        }
    }

    // Handle offer message
    void handleOffer(const std::string& peer_id, const std::string& content) {
        auto task = [this, peer_id, content]() -> core::Task<void> {
            // Parse offer
            WebRtcSessionDescription offer;
            try {
                offer = WebRtcSessionDescription::fromJson(content);
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to parse offer: {}", e.what());
                onError.emit(e);
                co_return;
            }

            // Get or create peer connection
            std::shared_ptr<WebRtcPeerConnection> pc;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = peer_connections_.find(peer_id);
                if (it != peer_connections_.end()) {
                    pc = it->second;
                } else {
                    pc = WebRtcPeerConnection::create(config_);
                    setupPeerConnectionEvents(pc, peer_id);
                    peer_connections_[peer_id] = pc;
                }
            }

            // Add local tracks
            for (const auto& stream : local_streams_) {
                for (const auto& track : stream->getTracks()) {
                    try {
                        pc->addTrack(track, {stream});
                    } catch (const WebRtcError& e) {
                        core::Logger::warn("Failed to add track: {}", e.what());
                    }
                }
            }

            try {
                // Set remote description
                co_await pc->setRemoteDescription(offer);

                // Create answer
                auto answer = co_await pc->createAnswer();

                // Set local description
                co_await pc->setLocalDescription(answer);

                // Send answer
                if (signaling_ && signaling_->state() == WebRtcSignalingState::Connected) {
                    WebRtcSignalingMessage message;
                    message.type = WebRtcSignalingMessageType::Answer;
                    message.sender_id = signaling_->clientId();
                    message.target_id = peer_id;
                    message.content = answer.toJson();

                    co_await signaling_->sendMessage(message);
                }
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to handle offer: {}", e.what());
                onError.emit(e);

                // Remove peer connection on error
                std::lock_guard<std::mutex> lock(mutex_);
                peer_connections_.erase(peer_id);
            }

            co_return;
        };

        // Execute task asynchronously
        task();
    }

    // Handle answer message
    void handleAnswer(const std::string& peer_id, const std::string& content) {
        auto task = [this, peer_id, content]() -> core::Task<void> {
            // Get peer connection
            auto pc = getPeerConnection(peer_id);
            if (!pc) {
                core::Logger::warn("Received answer for unknown peer: {}", peer_id);
                co_return;
            }

            // Parse answer
            WebRtcSessionDescription answer;
            try {
                answer = WebRtcSessionDescription::fromJson(content);
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to parse answer: {}", e.what());
                onError.emit(e);
                co_return;
            }

            // Set remote description
            try {
                co_await pc->setRemoteDescription(answer);
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to set remote description: {}", e.what());
                onError.emit(e);
            }

            co_return;
        };

        // Execute task asynchronously
        task();
    }

    // Handle ICE candidate message
    void handleIceCandidate(const std::string& peer_id, const std::string& content) {
        auto task = [this, peer_id, content]() -> core::Task<void> {
            // Get peer connection
            auto pc = getPeerConnection(peer_id);
            if (!pc) {
                core::Logger::warn("Received ICE candidate for unknown peer: {}", peer_id);
                co_return;
            }

            // Parse candidate
            WebRtcIceCandidate candidate;
            try {
                candidate = WebRtcIceCandidate::fromJson(content);
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to parse ICE candidate: {}", e.what());
                onError.emit(e);
                co_return;
            }

            // Add candidate
            try {
                co_await pc->addIceCandidate(candidate);
            } catch (const WebRtcError& e) {
                core::Logger::error("Failed to add ICE candidate: {}", e.what());
                onError.emit(e);
            }

            co_return;
        };

        // Execute task asynchronously
        task();
    }

    // Handle hangup message
    void handleHangup(const std::string& peer_id) {
        disconnect(peer_id);
    }

    // Handle error message
    void handleError(const std::string& peer_id, const std::string& content) {
        core::Logger::error("Received error from peer {}: {}", peer_id, content);

        // Create error object
        WebRtcError error(WebRtcErrorCode::SignalingFailed,
                       "Signaling error from peer " + peer_id + ": " + content);

        // Emit error
        onError.emit(error);
    }

    // Signaling
    std::shared_ptr<WebRtcSignaling> signaling_;

    // Configuration
    WebRtcConfiguration config_;

    // Running state
    bool is_running_ = false;

    // Peer connections
    std::map<std::string, std::shared_ptr<WebRtcPeerConnection>> peer_connections_;

    // Local streams
    std::vector<std::shared_ptr<media::MediaStream>> local_streams_;

    // Remote streams by peer ID
    std::map<std::string, std::vector<std::shared_ptr<media::MediaStream>>> remote_streams_;

    // Mutex for thread safety
    mutable std::mutex mutex_;
};

// Factory method implementation
std::shared_ptr<WebRtcSession> createSessionImpl(
    std::shared_ptr<WebRtcSignaling> signaling,
    const WebRtcConfiguration& config) {
    return std::make_shared<WebRtcSessionImpl>(signaling, config);
}

} // namespace fmus::webrtc