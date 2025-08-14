#include "fmus/webrtc/session.hpp"
#include "fmus/core/logger.hpp"

namespace fmus::webrtc {

Session::Session() {
    fmus::core::Logger::debug("WebRTC Session created");
}

Session::~Session() {
    if (running_) {
        stop();
    }
    fmus::core::Logger::debug("WebRTC Session destroyed");
}

bool Session::start() {
    if (running_) {
        fmus::core::Logger::warn("WebRTC Session already running");
        return false;
    }
    
    fmus::core::Logger::info("Starting WebRTC Session");
    running_ = true;
    state_ = SessionState::Connecting;
    
    if (onStateChange) {
        onStateChange(state_);
    }
    
    // Simulate connection
    state_ = SessionState::Connected;
    if (onStateChange) {
        onStateChange(state_);
    }
    
    return true;
}

void Session::stop() {
    if (!running_) {
        return;
    }
    
    fmus::core::Logger::info("Stopping WebRTC Session");
    running_ = false;
    state_ = SessionState::Closed;
    
    if (onStateChange) {
        onStateChange(state_);
    }
}

} // namespace fmus::webrtc
