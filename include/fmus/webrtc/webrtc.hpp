#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include <chrono>
#include <functional>
#include <optional>
#include <map>
#include <set>
#include <atomic>

#include <fmus/core/error.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/event.hpp>
#include <fmus/media/media.hpp>
#include <fmus/rtp/rtp.hpp>
#include <fmus/network/network.hpp>

namespace fmus::webrtc {

// Forward declarations
class WebRtcPeerConnection;
class WebRtcDataChannel;
class WebRtcIceCandidate;
class WebRtcSessionDescription;
class WebRtcRtpSender;
class WebRtcRtpReceiver;
class WebRtcRtpTransceiver;
class WebRtcMediaTrack;
class WebRtcSignaling;

// WebRTC error codes
enum class WebRtcErrorCode {
    Success = 0,
    ConnectionFailed,
    InvalidParameter,
    SignalingFailed,
    NegotiationFailed,
    MediaStreamError,
    DataChannelError,
    IceError,
    NotInitialized,
    AlreadyInitialized,
    NotSupported,
    UnknownError
};

// WebRTC error exception
class WebRtcError : public core::Error {
public:
    explicit WebRtcError(WebRtcErrorCode code, const std::string& message);
    WebRtcErrorCode webrtcCode() const noexcept { return code_; }

private:
    WebRtcErrorCode code_;
};

// WebRTC ICE server configuration
struct WebRtcIceServer {
    std::string urls;
    std::optional<std::string> username;
    std::optional<std::string> credential;
};

// WebRTC configuration
struct WebRtcConfiguration {
    std::vector<WebRtcIceServer> ice_servers;
    bool ice_transport_policy_relay = false;
    bool bundle_policy = true;
    bool rtcp_mux_policy = true;
    std::chrono::seconds ice_candidate_pool_size = std::chrono::seconds(0);
};

// ICE candidate
class WebRtcIceCandidate {
public:
    WebRtcIceCandidate() = default;
    WebRtcIceCandidate(const std::string& sdp_mid,
                      int sdp_mline_index,
                      const std::string& candidate);

    std::string sdpMid() const { return sdp_mid_; }
    int sdpMLineIndex() const { return sdp_mline_index_; }
    std::string candidate() const { return candidate_; }
    std::string toJson() const;

    static WebRtcIceCandidate fromJson(const std::string& json);

private:
    std::string sdp_mid_;
    int sdp_mline_index_ = 0;
    std::string candidate_;
};

// SDP session description types
enum class WebRtcSdpType {
    Offer,
    Answer,
    PrAnswer,  // Provisional answer
    Rollback   // Rollback to previous stable state
};

// Session description
class WebRtcSessionDescription {
public:
    WebRtcSessionDescription() = default;
    WebRtcSessionDescription(WebRtcSdpType type, const std::string& sdp);

    WebRtcSdpType type() const { return type_; }
    std::string sdp() const { return sdp_; }
    std::string typeString() const;
    std::string toJson() const;

    static WebRtcSessionDescription fromJson(const std::string& json);

private:
    WebRtcSdpType type_ = WebRtcSdpType::Offer;
    std::string sdp_;
};

// Signaling states
enum class WebRtcSignalingState {
    Stable,
    HaveLocalOffer,
    HaveRemoteOffer,
    HaveLocalPrAnswer,
    HaveRemotePrAnswer,
    Closed
};

// ICE connection states
enum class WebRtcIceConnectionState {
    New,
    Checking,
    Connected,
    Completed,
    Failed,
    Disconnected,
    Closed
};

// ICE gathering states
enum class WebRtcIceGatheringState {
    New,
    Gathering,
    Complete
};

// Connection states
enum class WebRtcPeerConnectionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed
};

// Data channel states
enum class WebRtcDataChannelState {
    Connecting,
    Open,
    Closing,
    Closed
};

// Data channel configuration
struct WebRtcDataChannelInit {
    bool ordered = true;
    std::optional<int> max_packet_life_time;
    std::optional<int> max_retransmits;
    std::string protocol;
    bool negotiated = false;
    std::optional<int> id;
};

// Data channel
class WebRtcDataChannel {
public:
    virtual ~WebRtcDataChannel() = default;

    // Properties
    virtual std::string label() const = 0;
    virtual bool ordered() const = 0;
    virtual std::optional<int> maxPacketLifeTime() const = 0;
    virtual std::optional<int> maxRetransmits() const = 0;
    virtual std::string protocol() const = 0;
    virtual bool negotiated() const = 0;
    virtual int id() const = 0;
    virtual uint64_t bufferedAmount() const = 0;
    virtual WebRtcDataChannelState state() const = 0;

    // Methods
    virtual void send(const std::string& data) = 0;
    virtual void send(const uint8_t* data, size_t size) = 0;
    virtual void close() = 0;

    // Events
    core::EventEmitter<const std::string&> onMessage;
    core::EventEmitter<const std::vector<uint8_t>&> onBinaryMessage;
    core::EventEmitter<WebRtcDataChannelState> onStateChange;
    core::EventEmitter<uint64_t> onBufferedAmountChange;
    core::EventEmitter<const WebRtcError&> onError;
};

// WebRTC track event
struct WebRtcTrackEvent {
    std::shared_ptr<WebRtcRtpReceiver> receiver;
    std::shared_ptr<media::MediaTrack> track;
    std::vector<std::shared_ptr<media::MediaStream>> streams;
    std::shared_ptr<WebRtcRtpTransceiver> transceiver;
};

// RTP transceiver direction
enum class WebRtcRtpTransceiverDirection {
    SendRecv,
    SendOnly,
    RecvOnly,
    Inactive
};

// RTP encoding parameters
struct WebRtcRtpEncodingParameters {
    std::optional<bool> active;
    std::optional<double> max_bitrate;
    std::optional<double> min_bitrate;
    std::optional<double> max_framerate;
    std::optional<double> scale_resolution_down_by;
    std::optional<std::string> rid;
};

// RTP send parameters
struct WebRtcRtpSendParameters {
    std::vector<WebRtcRtpEncodingParameters> encodings;
};

// RTP capabilities
struct WebRtcRtpCapabilities {
    std::vector<rtp::RtpPayloadType> codecs;
    // Other capabilities like header extensions, etc.
};

// RTP sender
class WebRtcRtpSender {
public:
    virtual ~WebRtcRtpSender() = default;

    // Properties
    virtual std::shared_ptr<media::MediaTrack> track() const = 0;
    virtual std::string trackId() const = 0;

    // Methods
    virtual core::Task<bool> replaceTrack(std::shared_ptr<media::MediaTrack> track) = 0;
    virtual WebRtcRtpSendParameters getParameters() const = 0;
    virtual core::Task<bool> setParameters(const WebRtcRtpSendParameters& parameters) = 0;
};

// RTP receiver
class WebRtcRtpReceiver {
public:
    virtual ~WebRtcRtpReceiver() = default;

    // Properties
    virtual std::shared_ptr<media::MediaTrack> track() const = 0;
    virtual std::string trackId() const = 0;

    // Methods
    virtual WebRtcRtpCapabilities getCapabilities() const = 0;
};

// RTP transceiver
class WebRtcRtpTransceiver {
public:
    virtual ~WebRtcRtpTransceiver() = default;

    // Properties
    virtual std::string mid() const = 0;
    virtual WebRtcRtpTransceiverDirection direction() const = 0;
    virtual std::optional<WebRtcRtpTransceiverDirection> currentDirection() const = 0;
    virtual std::shared_ptr<WebRtcRtpSender> sender() const = 0;
    virtual std::shared_ptr<WebRtcRtpReceiver> receiver() const = 0;
    virtual bool stopped() const = 0;

    // Methods
    virtual void stop() = 0;
    virtual void setDirection(WebRtcRtpTransceiverDirection direction) = 0;
};

// WebRTC peer connection
class WebRtcPeerConnection {
public:
    virtual ~WebRtcPeerConnection() = default;

    // Connection management
    virtual core::Task<WebRtcSessionDescription> createOffer() = 0;
    virtual core::Task<WebRtcSessionDescription> createAnswer() = 0;
    virtual core::Task<void> setLocalDescription(const WebRtcSessionDescription& desc) = 0;
    virtual core::Task<void> setRemoteDescription(const WebRtcSessionDescription& desc) = 0;
    virtual std::optional<WebRtcSessionDescription> localDescription() const = 0;
    virtual std::optional<WebRtcSessionDescription> remoteDescription() const = 0;
    virtual std::optional<WebRtcSessionDescription> currentLocalDescription() const = 0;
    virtual std::optional<WebRtcSessionDescription> currentRemoteDescription() const = 0;
    virtual std::optional<WebRtcSessionDescription> pendingLocalDescription() const = 0;
    virtual std::optional<WebRtcSessionDescription> pendingRemoteDescription() const = 0;

    // ICE candidates
    virtual core::Task<void> addIceCandidate(const WebRtcIceCandidate& candidate) = 0;
    virtual void restartIce() = 0;
    virtual WebRtcSignalingState signalingState() const = 0;
    virtual WebRtcIceGatheringState iceGatheringState() const = 0;
    virtual WebRtcIceConnectionState iceConnectionState() const = 0;
    virtual WebRtcPeerConnectionState connectionState() const = 0;
    virtual bool canTrickleIceCandidates() const = 0;

    // Media management
    virtual std::vector<std::shared_ptr<WebRtcRtpSender>> getSenders() const = 0;
    virtual std::vector<std::shared_ptr<WebRtcRtpReceiver>> getReceivers() const = 0;
    virtual std::vector<std::shared_ptr<WebRtcRtpTransceiver>> getTransceivers() const = 0;
    virtual std::shared_ptr<WebRtcRtpSender> addTrack(
        std::shared_ptr<media::MediaTrack> track,
        const std::vector<std::shared_ptr<media::MediaStream>>& streams = {}) = 0;
    virtual void removeTrack(std::shared_ptr<WebRtcRtpSender> sender) = 0;
    virtual std::shared_ptr<WebRtcRtpTransceiver> addTransceiver(
        std::shared_ptr<media::MediaTrack> track,
        WebRtcRtpTransceiverDirection direction = WebRtcRtpTransceiverDirection::SendRecv) = 0;
    virtual std::shared_ptr<WebRtcRtpTransceiver> addTransceiver(
        media::MediaType kind,
        WebRtcRtpTransceiverDirection direction = WebRtcRtpTransceiverDirection::SendRecv) = 0;

    // Data channels
    virtual std::shared_ptr<WebRtcDataChannel> createDataChannel(
        const std::string& label,
        const WebRtcDataChannelInit& options = {}) = 0;

    // Connection control
    virtual void close() = 0;

    // Statistics
    virtual core::Task<std::string> getStats() = 0;

    // Factory method
    static std::shared_ptr<WebRtcPeerConnection> create(const WebRtcConfiguration& config);

    // Events
    core::EventEmitter<WebRtcIceConnectionState> onIceConnectionStateChange;
    core::EventEmitter<WebRtcIceGatheringState> onIceGatheringStateChange;
    core::EventEmitter<const WebRtcIceCandidate&> onIceCandidate;
    core::EventEmitter<WebRtcSignalingState> onSignalingStateChange;
    core::EventEmitter<WebRtcPeerConnectionState> onConnectionStateChange;
    core::EventEmitter<std::shared_ptr<WebRtcDataChannel>> onDataChannel;
    core::EventEmitter<WebRtcTrackEvent> onTrack;
    core::EventEmitter<void> onNegotiationNeeded;
    core::EventEmitter<const WebRtcError&> onError;
};

// Signaling channel message types
enum class WebRtcSignalingMessageType {
    Offer,
    Answer,
    IceCandidate,
    Hangup,
    Error
};

// Signaling channel message
struct WebRtcSignalingMessage {
    WebRtcSignalingMessageType type;
    std::string sender_id;
    std::string target_id;
    std::string content;
};

// Signaling channel state
enum class WebRtcSignalingState {
    Disconnected,
    Connecting,
    Connected,
    Failed
};

// Signaling channel interface
class WebRtcSignaling {
public:
    virtual ~WebRtcSignaling() = default;

    // Connect/disconnect to signaling server
    virtual core::Task<void> connect(const std::string& server_url) = 0;
    virtual void disconnect() = 0;

    // Join/leave room
    virtual core::Task<std::vector<std::string>> join(const std::string& room_id, const std::string& client_id) = 0;
    virtual void leave() = 0;

    // Send message
    virtual core::Task<void> sendMessage(const WebRtcSignalingMessage& message) = 0;

    // State
    virtual WebRtcSignalingState state() const = 0;
    virtual std::string currentRoom() const = 0;
    virtual std::string clientId() const = 0;

    // Factory method
    static std::unique_ptr<WebRtcSignaling> create();

    // Events
    core::EventEmitter<WebRtcSignalingState> onStateChange;
    core::EventEmitter<const WebRtcSignalingMessage&> onMessage;
    core::EventEmitter<const std::string&> onClientJoined;
    core::EventEmitter<const std::string&> onClientLeft;
    core::EventEmitter<const WebRtcError&> onError;
};

// WebRTC session
class WebRtcSession {
public:
    virtual ~WebRtcSession() = default;

    // Session management
    virtual core::Task<void> start() = 0;
    virtual void stop() = 0;

    // Peer management
    virtual core::Task<void> connect(const std::string& peer_id) = 0;
    virtual void disconnect(const std::string& peer_id) = 0;
    virtual void disconnectAll() = 0;
    virtual std::set<std::string> getConnectedPeers() const = 0;
    virtual std::shared_ptr<WebRtcPeerConnection> getPeerConnection(const std::string& peer_id) const = 0;

    // Media management
    virtual core::Task<void> addLocalStream(std::shared_ptr<media::MediaStream> stream) = 0;
    virtual void removeLocalStream(std::shared_ptr<media::MediaStream> stream) = 0;
    virtual std::vector<std::shared_ptr<media::MediaStream>> getLocalStreams() const = 0;
    virtual std::map<std::string, std::vector<std::shared_ptr<media::MediaStream>>> getRemoteStreams() const = 0;

    // Data channels
    virtual std::shared_ptr<WebRtcDataChannel> createDataChannel(
        const std::string& peer_id,
        const std::string& label,
        const WebRtcDataChannelInit& options = {}) = 0;

    // Factory method
    static std::shared_ptr<WebRtcSession> create(
        std::shared_ptr<WebRtcSignaling> signaling,
        const WebRtcConfiguration& config);

    // Events
    core::EventEmitter<const std::string&> onPeerConnected;
    core::EventEmitter<const std::string&> onPeerDisconnected;
    core::EventEmitter<std::pair<std::string, WebRtcTrackEvent>> onRemoteTrack;
    core::EventEmitter<std::pair<std::string, std::shared_ptr<WebRtcDataChannel>>> onDataChannel;
    core::EventEmitter<const WebRtcError&> onError;
};

} // namespace fmus::webrtc