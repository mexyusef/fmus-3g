#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>
#include <nlohmann/json.hpp>

namespace fmus::webrtc {

// WebRtcError implementation
WebRtcError::WebRtcError(WebRtcErrorCode code, const std::string& message)
    : core::Error(core::ErrorCode::WebRtcError, message), code_(code) {}

// WebRtcIceCandidate implementation
WebRtcIceCandidate::WebRtcIceCandidate(const std::string& sdp_mid,
                                     int sdp_mline_index,
                                     const std::string& candidate)
    : sdp_mid_(sdp_mid), sdp_mline_index_(sdp_mline_index), candidate_(candidate) {}

std::string WebRtcIceCandidate::toJson() const {
    // Membuat representasi JSON dari ICE candidate
    nlohmann::json j;
    j["sdpMid"] = sdp_mid_;
    j["sdpMLineIndex"] = sdp_mline_index_;
    j["candidate"] = candidate_;
    return j.dump();
}

WebRtcIceCandidate WebRtcIceCandidate::fromJson(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        return WebRtcIceCandidate(
            j["sdpMid"].get<std::string>(),
            j["sdpMLineIndex"].get<int>(),
            j["candidate"].get<std::string>()
        );
    } catch (const std::exception& e) {
        core::Logger::error("Failed to parse ICE candidate from JSON: {}", e.what());
        throw WebRtcError(WebRtcErrorCode::InvalidParameter,
                       "Failed to parse ICE candidate: " + std::string(e.what()));
    }
}

// WebRtcSessionDescription implementation
WebRtcSessionDescription::WebRtcSessionDescription(WebRtcSdpType type, const std::string& sdp)
    : type_(type), sdp_(sdp) {}

std::string WebRtcSessionDescription::typeString() const {
    switch (type_) {
        case WebRtcSdpType::Offer: return "offer";
        case WebRtcSdpType::Answer: return "answer";
        case WebRtcSdpType::PrAnswer: return "pranswer";
        case WebRtcSdpType::Rollback: return "rollback";
        default: return "unknown";
    }
}

std::string WebRtcSessionDescription::toJson() const {
    nlohmann::json j;
    j["type"] = typeString();
    j["sdp"] = sdp_;
    return j.dump();
}

WebRtcSessionDescription WebRtcSessionDescription::fromJson(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);

        WebRtcSdpType type;
        std::string type_str = j["type"].get<std::string>();

        if (type_str == "offer") type = WebRtcSdpType::Offer;
        else if (type_str == "answer") type = WebRtcSdpType::Answer;
        else if (type_str == "pranswer") type = WebRtcSdpType::PrAnswer;
        else if (type_str == "rollback") type = WebRtcSdpType::Rollback;
        else throw std::invalid_argument("Unknown SDP type");

        return WebRtcSessionDescription(type, j["sdp"].get<std::string>());
    } catch (const std::exception& e) {
        core::Logger::error("Failed to parse SDP from JSON: {}", e.what());
        throw WebRtcError(WebRtcErrorCode::InvalidParameter,
                       "Failed to parse SDP: " + std::string(e.what()));
    }
}

// Forward declarations
class WebRtcPeerConnectionImpl;
class WebRtcSignalingImpl;
class WebRtcSessionImpl;

// Factory implementations
std::shared_ptr<WebRtcPeerConnection> WebRtcPeerConnection::create(const WebRtcConfiguration& config) {
    core::Logger::info("Creating WebRTC peer connection");
    // We include the actual implementation in peer_connection.cpp
    extern std::shared_ptr<WebRtcPeerConnection> createPeerConnectionImpl(const WebRtcConfiguration& config);
    return createPeerConnectionImpl(config);
}

std::unique_ptr<WebRtcSignaling> WebRtcSignaling::create() {
    core::Logger::info("Creating WebRTC signaling");
    // We include the actual implementation in signaling.cpp
    extern std::unique_ptr<WebRtcSignaling> createSignalingImpl();
    return createSignalingImpl();
}

std::shared_ptr<WebRtcSession> WebRtcSession::create(
    std::shared_ptr<WebRtcSignaling> signaling,
    const WebRtcConfiguration& config) {
    core::Logger::info("Creating WebRTC session with configuration");
    // We include the actual implementation in session.cpp
    extern std::shared_ptr<WebRtcSession> createSessionImpl(
        std::shared_ptr<WebRtcSignaling> signaling,
        const WebRtcConfiguration& config);
    return createSessionImpl(signaling, config);
}

} // namespace fmus::webrtc