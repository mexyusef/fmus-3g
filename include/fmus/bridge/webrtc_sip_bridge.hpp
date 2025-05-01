#pragma once

#include <fmus/webrtc/webrtc.hpp>
#include <fmus/sip/sip.hpp>
#include <fmus/core/task.hpp>
#include <fmus/media/media.hpp>

#include <string>
#include <memory>
#include <map>
#include <mutex>

namespace fmus::bridge {

/**
 * Bridge configuration options
 */
struct WebRtcSipBridgeConfig {
    // WebRTC configuration
    webrtc::WebRtcConfiguration webrtc_config;
    std::string signaling_url = "wss://example.com/signaling";
    std::string room_id = "default-room";

    // SIP configuration
    std::string sip_uri = "sip:user@example.com";
    std::string sip_password;
    std::string sip_proxy;

    // Media configuration
    bool audio_enabled = true;
    bool video_enabled = true;

    // Transcoding options
    bool transcode_audio = false;
    bool transcode_video = false;
    std::string audio_codec = "PCMU";
    std::string video_codec = "H264";
};

/**
 * Events emitted by the bridge
 */
enum class WebRtcSipBridgeEvent {
    SipRegistered,
    SipRegistrationFailed,
    SipIncomingCall,
    SipCallConnected,
    SipCallDisconnected,
    WebRtcClientConnected,
    WebRtcClientDisconnected,
    BridgeStarted,
    BridgeStopped,
    Error
};

/**
 * A bridge that interconnects WebRTC clients with SIP endpoints.
 * This allows WebRTC clients to communicate with SIP phones, PBXs, etc.
 */
class WebRtcSipBridge : public core::EventEmitter<WebRtcSipBridgeEvent> {
public:
    virtual ~WebRtcSipBridge() = default;

    /**
     * Start the bridge
     */
    virtual core::Task<void> start() = 0;

    /**
     * Stop the bridge
     */
    virtual void stop() = 0;

    /**
     * Check if the bridge is running
     */
    virtual bool isRunning() const = 0;

    /**
     * Make an outbound call from the SIP side to a specified URI
     * and bridge it to connected WebRTC clients
     *
     * @param uri Target SIP URI to call
     * @return A task that completes when the call setup is initiated
     */
    virtual core::Task<void> makeOutboundCall(const std::string& uri) = 0;

    /**
     * Add a WebRTC client as a recipient of the bridged call
     *
     * @param client_id ID of the WebRTC client
     * @return A task that completes when the client is added
     */
    virtual core::Task<void> addWebRtcClient(const std::string& client_id) = 0;

    /**
     * Remove a WebRTC client from the bridge
     *
     * @param client_id ID of the WebRTC client to remove
     */
    virtual void removeWebRtcClient(const std::string& client_id) = 0;

    /**
     * Get the list of connected WebRTC clients
     */
    virtual std::vector<std::string> getConnectedClients() const = 0;

    /**
     * Get the SIP call status
     */
    virtual sip::SipCallState getSipCallState() const = 0;

    /**
     * Get the SIP registration status
     */
    virtual sip::SipRegistrationState getSipRegistrationState() const = 0;

    /**
     * Factory method to create a bridge
     */
    static std::shared_ptr<WebRtcSipBridge> create(const WebRtcSipBridgeConfig& config);
};

} // namespace fmus::bridge