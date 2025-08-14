#pragma once

#include <string>
#include <memory>
#include <functional>

namespace fmus::webrtc {

enum class SessionState {
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed
};

class Session {
public:
    Session();
    ~Session();
    
    // Basic session management
    bool start();
    void stop();
    
    SessionState getState() const { return state_; }
    
    // Event callbacks
    std::function<void(SessionState)> onStateChange;
    std::function<void(const std::string&)> onError;

private:
    SessionState state_ = SessionState::New;
    bool running_ = false;
};

} // namespace fmus::webrtc
