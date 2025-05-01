#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <memory>
#include <map>

namespace fmus::webrtc {

// WebRTC peer connection implementation
class WebRtcPeerConnectionImpl : public WebRtcPeerConnection,
                               public std::enable_shared_from_this<WebRtcPeerConnectionImpl> {
public:
    WebRtcPeerConnectionImpl(const WebRtcConfiguration& config)
        : config_(config), state_(WebRtcPeerConnectionState::New) {
        core::Logger::info("WebRtcPeerConnectionImpl created");
    }

    ~WebRtcPeerConnectionImpl() {
        close();
        core::Logger::info("WebRtcPeerConnectionImpl destroyed");
    }

    // Connection management
    core::Task<WebRtcSessionDescription> createOffer() override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Creating offer");

        // Contoh SDP yang disederhanakan (dalam implementasi nyata, ini akan dibuat oleh library WebRTC)
        std::string sdp = "v=0\r\n"
                        "o=- " + std::to_string(rand()) + " 2 IN IP4 127.0.0.1\r\n"
                        "s=-\r\n"
                        "t=0 0\r\n"
                        "a=group:BUNDLE audio video\r\n"
                        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                        "c=IN IP4 0.0.0.0\r\n"
                        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                        "a=ice-ufrag:dummy\r\n"
                        "a=ice-pwd:dummy\r\n"
                        "a=fingerprint:sha-256 dummy\r\n"
                        "a=setup:actpass\r\n"
                        "a=mid:audio\r\n"
                        "a=sendrecv\r\n"
                        "a=rtpmap:111 opus/48000/2\r\n"
                        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                        "c=IN IP4 0.0.0.0\r\n"
                        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                        "a=ice-ufrag:dummy\r\n"
                        "a=ice-pwd:dummy\r\n"
                        "a=fingerprint:sha-256 dummy\r\n"
                        "a=setup:actpass\r\n"
                        "a=mid:video\r\n"
                        "a=sendrecv\r\n"
                        "a=rtpmap:96 VP8/90000\r\n";

        // Mengubah status signaling
        setSignalingState(WebRtcSignalingState::HaveLocalOffer);

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(100));

        co_return WebRtcSessionDescription(WebRtcSdpType::Offer, sdp);
    }

    core::Task<WebRtcSessionDescription> createAnswer() override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Harus memiliki remote offer
        if (signaling_state_ != WebRtcSignalingState::HaveRemoteOffer &&
            signaling_state_ != WebRtcSignalingState::HaveRemotePrAnswer) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                             "Cannot create answer in signaling state: " +
                             std::to_string(static_cast<int>(signaling_state_)));
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Creating answer");

        // Contoh SDP yang disederhanakan (dalam implementasi nyata, ini akan dibuat oleh library WebRTC)
        std::string sdp = "v=0\r\n"
                        "o=- " + std::to_string(rand()) + " 2 IN IP4 127.0.0.1\r\n"
                        "s=-\r\n"
                        "t=0 0\r\n"
                        "a=group:BUNDLE audio video\r\n"
                        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                        "c=IN IP4 0.0.0.0\r\n"
                        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                        "a=ice-ufrag:dummy\r\n"
                        "a=ice-pwd:dummy\r\n"
                        "a=fingerprint:sha-256 dummy\r\n"
                        "a=setup:passive\r\n"
                        "a=mid:audio\r\n"
                        "a=sendrecv\r\n"
                        "a=rtpmap:111 opus/48000/2\r\n"
                        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                        "c=IN IP4 0.0.0.0\r\n"
                        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                        "a=ice-ufrag:dummy\r\n"
                        "a=ice-pwd:dummy\r\n"
                        "a=fingerprint:sha-256 dummy\r\n"
                        "a=setup:passive\r\n"
                        "a=mid:video\r\n"
                        "a=sendrecv\r\n"
                        "a=rtpmap:96 VP8/90000\r\n";

        // Mengubah status signaling
        setSignalingState(WebRtcSignalingState::Stable);

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(100));

        co_return WebRtcSessionDescription(WebRtcSdpType::Answer, sdp);
    }

    core::Task<void> setLocalDescription(const WebRtcSessionDescription& desc) override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Setting local description: {}", desc.typeString());

        // Update local description
        local_description_ = desc;

        // Update signaling state berdasarkan tipe SDP
        switch (desc.type()) {
            case WebRtcSdpType::Offer:
                setSignalingState(WebRtcSignalingState::HaveLocalOffer);
                break;
            case WebRtcSdpType::Answer:
                setSignalingState(WebRtcSignalingState::Stable);
                break;
            case WebRtcSdpType::PrAnswer:
                setSignalingState(WebRtcSignalingState::HaveLocalPrAnswer);
                break;
            case WebRtcSdpType::Rollback:
                setSignalingState(WebRtcSignalingState::Stable);
                break;
        }

        // Memicu ICE gathering jika ini adalah offer atau answer
        if (desc.type() == WebRtcSdpType::Offer || desc.type() == WebRtcSdpType::Answer) {
            simulateIceGathering();
        }

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(50));

        co_return;
    }

    core::Task<void> setRemoteDescription(const WebRtcSessionDescription& desc) override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Setting remote description: {}", desc.typeString());

        // Update remote description
        remote_description_ = desc;

        // Update signaling state berdasarkan tipe SDP
        switch (desc.type()) {
            case WebRtcSdpType::Offer:
                setSignalingState(WebRtcSignalingState::HaveRemoteOffer);
                break;
            case WebRtcSdpType::Answer:
                setSignalingState(WebRtcSignalingState::Stable);
                break;
            case WebRtcSdpType::PrAnswer:
                setSignalingState(WebRtcSignalingState::HaveRemotePrAnswer);
                break;
            case WebRtcSdpType::Rollback:
                setSignalingState(WebRtcSignalingState::Stable);
                break;
        }

        // Simulate establishing connection if we're now stable
        if (signaling_state_ == WebRtcSignalingState::Stable) {
            simulateConnectionEstablishment();
        }

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(50));

        co_return;
    }

    std::optional<WebRtcSessionDescription> localDescription() const override {
        return local_description_;
    }

    std::optional<WebRtcSessionDescription> remoteDescription() const override {
        return remote_description_;
    }

    std::optional<WebRtcSessionDescription> currentLocalDescription() const override {
        return current_local_description_;
    }

    std::optional<WebRtcSessionDescription> currentRemoteDescription() const override {
        return current_remote_description_;
    }

    std::optional<WebRtcSessionDescription> pendingLocalDescription() const override {
        return pending_local_description_;
    }

    std::optional<WebRtcSessionDescription> pendingRemoteDescription() const override {
        return pending_remote_description_;
    }

    // ICE candidates
    core::Task<void> addIceCandidate(const WebRtcIceCandidate& candidate) override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Dalam implementasi nyata, kandidat akan ditambahkan ke ICE agent
        core::Logger::info("Adding ICE candidate: {}", candidate.candidate());

        // Add to our list of remote candidates
        remote_candidates_.push_back(candidate);

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(20));

        co_return;
    }

    void restartIce() override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        core::Logger::info("Restarting ICE");

        // Reset ICE connection state
        setIceConnectionState(WebRtcIceConnectionState::New);
        setIceGatheringState(WebRtcIceGatheringState::New);

        // Clear candidates
        local_candidates_.clear();
        remote_candidates_.clear();

        // Trigger negotiation needed
        onNegotiationNeeded.emit();
    }

    WebRtcSignalingState signalingState() const override {
        return signaling_state_;
    }

    WebRtcIceGatheringState iceGatheringState() const override {
        return ice_gathering_state_;
    }

    WebRtcIceConnectionState iceConnectionState() const override {
        return ice_connection_state_;
    }

    WebRtcPeerConnectionState connectionState() const override {
        return state_;
    }

    bool canTrickleIceCandidates() const override {
        // Trickle ICE always supported in this implementation
        return true;
    }

    // Media management
    std::vector<std::shared_ptr<WebRtcRtpSender>> getSenders() const override {
        return senders_;
    }

    std::vector<std::shared_ptr<WebRtcRtpReceiver>> getReceivers() const override {
        return receivers_;
    }

    std::vector<std::shared_ptr<WebRtcRtpTransceiver>> getTransceivers() const override {
        return transceivers_;
    }

    std::shared_ptr<WebRtcRtpSender> addTrack(
        std::shared_ptr<media::MediaTrack> track,
        const std::vector<std::shared_ptr<media::MediaStream>>& streams) override {

        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Adding track: {}", track->id());

        // Check if track already exists
        for (const auto& sender : senders_) {
            if (sender->trackId() == track->id()) {
                throw WebRtcError(WebRtcErrorCode::InvalidParameter,
                                "Track already exists: " + track->id());
            }
        }

        // Dalam implementasi nyata, create a sender with the track
        // For now, just log and return a dummy sender

        // Trigger negotation needed
        onNegotiationNeeded.emit();

        // Dummy implementation
        throw WebRtcError(WebRtcErrorCode::NotImplemented, "addTrack not fully implemented yet");
    }

    void removeTrack(std::shared_ptr<WebRtcRtpSender> sender) override {
        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Find and remove the sender
        for (auto it = senders_.begin(); it != senders_.end(); ++it) {
            if (*it == sender) {
                core::Logger::info("Removing track: {}", sender->trackId());
                senders_.erase(it);

                // Trigger negotation needed
                onNegotiationNeeded.emit();
                return;
            }
        }

        throw WebRtcError(WebRtcErrorCode::InvalidParameter, "Sender not found");
    }

    std::shared_ptr<WebRtcRtpTransceiver> addTransceiver(
        std::shared_ptr<media::MediaTrack> track,
        WebRtcRtpTransceiverDirection direction) override {

        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Adding transceiver for track: {}", track->id());

        // Dummy implementation
        throw WebRtcError(WebRtcErrorCode::NotImplemented, "addTransceiver not implemented yet");
    }

    std::shared_ptr<WebRtcRtpTransceiver> addTransceiver(
        media::MediaType kind,
        WebRtcRtpTransceiverDirection direction) override {

        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Adding transceiver for media type: {}",
                         kind == media::MediaType::Audio ? "audio" : "video");

        // Dummy implementation
        throw WebRtcError(WebRtcErrorCode::NotImplemented, "addTransceiver not implemented yet");
    }

    // Data channels
    std::shared_ptr<WebRtcDataChannel> createDataChannel(
        const std::string& label,
        const WebRtcDataChannelInit& options) override {

        // Memeriksa status koneksi
        if (state_ == WebRtcPeerConnectionState::Closed) {
            throw WebRtcError(WebRtcErrorCode::InvalidState, "Peer connection is closed");
        }

        // Implementasi dummy untuk contoh
        core::Logger::info("Creating data channel: {}", label);

        // Dummy implementation
        throw WebRtcError(WebRtcErrorCode::NotImplemented, "createDataChannel not implemented yet");
    }

    // Connection control
    void close() override {
        if (state_ == WebRtcPeerConnectionState::Closed) {
            return;
        }

        core::Logger::info("Closing peer connection");

        // Update state
        setState(WebRtcPeerConnectionState::Closed);
        setSignalingState(WebRtcSignalingState::Closed);
        setIceConnectionState(WebRtcIceConnectionState::Closed);
        setIceGatheringState(WebRtcIceGatheringState::Complete);

        // Close all data channels
        for (auto& dc : data_channels_) {
            dc->close();
        }

        // Clear collections
        senders_.clear();
        receivers_.clear();
        transceivers_.clear();
        data_channels_.clear();
        local_candidates_.clear();
        remote_candidates_.clear();
    }

    // Statistics
    core::Task<std::string> getStats() override {
        // Implementasi dummy untuk contoh
        core::Logger::info("Getting stats");

        // Mensimulasikan delay jaringan
        co_await core::sleep_for(std::chrono::milliseconds(10));

        // Return dummy JSON
        co_return "{\"dummy\":\"stats\"}";
    }

private:
    // Helper methods to update states and emit events
    void setState(WebRtcPeerConnectionState new_state) {
        if (state_ != new_state) {
            state_ = new_state;
            onConnectionStateChange.emit(state_);
        }
    }

    void setSignalingState(WebRtcSignalingState new_state) {
        if (signaling_state_ != new_state) {
            signaling_state_ = new_state;
            onSignalingStateChange.emit(signaling_state_);
        }
    }

    void setIceConnectionState(WebRtcIceConnectionState new_state) {
        if (ice_connection_state_ != new_state) {
            ice_connection_state_ = new_state;
            onIceConnectionStateChange.emit(ice_connection_state_);

            // Update overall connection state
            updateConnectionState();
        }
    }

    void setIceGatheringState(WebRtcIceGatheringState new_state) {
        if (ice_gathering_state_ != new_state) {
            ice_gathering_state_ = new_state;
            onIceGatheringStateChange.emit(ice_gathering_state_);
        }
    }

    void updateConnectionState() {
        // Derive the peer connection state from ICE connection state
        // In a real implementation, this would consider DTLS state as well
        WebRtcPeerConnectionState new_state;

        switch (ice_connection_state_) {
            case WebRtcIceConnectionState::New:
                new_state = WebRtcPeerConnectionState::New;
                break;
            case WebRtcIceConnectionState::Checking:
                new_state = WebRtcPeerConnectionState::Connecting;
                break;
            case WebRtcIceConnectionState::Connected:
            case WebRtcIceConnectionState::Completed:
                new_state = WebRtcPeerConnectionState::Connected;
                break;
            case WebRtcIceConnectionState::Disconnected:
                new_state = WebRtcPeerConnectionState::Disconnected;
                break;
            case WebRtcIceConnectionState::Failed:
                new_state = WebRtcPeerConnectionState::Failed;
                break;
            case WebRtcIceConnectionState::Closed:
                new_state = WebRtcPeerConnectionState::Closed;
                break;
            default:
                new_state = WebRtcPeerConnectionState::New;
                break;
        }

        setState(new_state);
    }

    // Simulate ICE gathering - used for demo purposes
    void simulateIceGathering() {
        // Start gathering
        setIceGatheringState(WebRtcIceGatheringState::Gathering);

        // Schedule some candidates to be generated
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Host candidate
            WebRtcIceCandidate host_candidate("audio", 0,
                "candidate:1 1 UDP 2130706431 192.168.1.1 50000 typ host");
            local_candidates_.push_back(host_candidate);
            onIceCandidate.emit(host_candidate);

            // Server-reflexive candidate (after 300ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            WebRtcIceCandidate srflx_candidate("audio", 0,
                "candidate:2 1 UDP 1694498815 203.0.113.1 50000 typ srflx raddr 192.168.1.1 rport 50000");
            local_candidates_.push_back(srflx_candidate);
            onIceCandidate.emit(srflx_candidate);

            // Relay candidate (after 500ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            WebRtcIceCandidate relay_candidate("audio", 0,
                "candidate:3 1 UDP 16777215 198.51.100.1 50000 typ relay raddr 203.0.113.1 rport 50000");
            local_candidates_.push_back(relay_candidate);
            onIceCandidate.emit(relay_candidate);

            // Complete gathering
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            setIceGatheringState(WebRtcIceGatheringState::Complete);
        }).detach();
    }

    // Simulate connection establishment - used for demo purposes
    void simulateConnectionEstablishment() {
        // Start connecting
        setIceConnectionState(WebRtcIceConnectionState::Checking);

        // Schedule connection states
        std::thread([this]() {
            // Connect after 500ms
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            setIceConnectionState(WebRtcIceConnectionState::Connected);

            // Complete after another 200ms
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            setIceConnectionState(WebRtcIceConnectionState::Completed);
        }).detach();
    }

    // Configuration
    WebRtcConfiguration config_;

    // State
    WebRtcPeerConnectionState state_ = WebRtcPeerConnectionState::New;
    WebRtcSignalingState signaling_state_ = WebRtcSignalingState::Stable;
    WebRtcIceConnectionState ice_connection_state_ = WebRtcIceConnectionState::New;
    WebRtcIceGatheringState ice_gathering_state_ = WebRtcIceGatheringState::New;

    // Session descriptions
    std::optional<WebRtcSessionDescription> local_description_;
    std::optional<WebRtcSessionDescription> remote_description_;
    std::optional<WebRtcSessionDescription> current_local_description_;
    std::optional<WebRtcSessionDescription> current_remote_description_;
    std::optional<WebRtcSessionDescription> pending_local_description_;
    std::optional<WebRtcSessionDescription> pending_remote_description_;

    // Media
    std::vector<std::shared_ptr<WebRtcRtpSender>> senders_;
    std::vector<std::shared_ptr<WebRtcRtpReceiver>> receivers_;
    std::vector<std::shared_ptr<WebRtcRtpTransceiver>> transceivers_;

    // Data channels
    std::vector<std::shared_ptr<WebRtcDataChannel>> data_channels_;

    // ICE candidates
    std::vector<WebRtcIceCandidate> local_candidates_;
    std::vector<WebRtcIceCandidate> remote_candidates_;
};

// Factory method implementation
std::shared_ptr<WebRtcPeerConnection> createPeerConnectionImpl(const WebRtcConfiguration& config) {
    return std::make_shared<WebRtcPeerConnectionImpl>(config);
}

} // namespace fmus::webrtc