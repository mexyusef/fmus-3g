#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>

#include <atomic>
#include <mutex>

namespace fmus::webrtc {

/**
 * Implementation class for WebRTC media track
 */
class WebRtcMediaTrackImpl : public WebRtcMediaTrack {
public:
    /**
     * Constructor
     */
    WebRtcMediaTrackImpl(std::string id, WebRtcMediaType type, bool enabled)
        : id_(std::move(id))
        , type_(type)
        , enabled_(enabled)
        , muted_(false)
        , state_(WebRtcMediaTrackState::Live) {
        core::Logger::debug("WebRtcMediaTrackImpl created with ID {}, type {}",
                          id_, type_ == WebRtcMediaType::Audio ? "Audio" : "Video");
    }

    /**
     * Destructor
     */
    ~WebRtcMediaTrackImpl() override {
        core::Logger::debug("WebRtcMediaTrackImpl destroyed with ID {}", id_);
    }

    /**
     * Get the track ID
     */
    std::string id() const override {
        return id_;
    }

    /**
     * Get the track type
     */
    WebRtcMediaType type() const override {
        return type_;
    }

    /**
     * Check if the track is enabled
     */
    bool enabled() const override {
        return enabled_;
    }

    /**
     * Enable or disable the track
     */
    void setEnabled(bool enabled) override {
        bool previous = enabled_;
        enabled_ = enabled;

        if (previous != enabled_) {
            core::Logger::debug("WebRtcMediaTrack {} {}", id_, enabled_ ? "enabled" : "disabled");
            emit<WebRtcMediaTrackEvent::EnabledChanged>(enabled_);
        }
    }

    /**
     * Check if the track is muted
     */
    bool muted() const override {
        return muted_;
    }

    /**
     * Mute or unmute the track
     */
    void setMuted(bool muted) override {
        bool previous = muted_;
        muted_ = muted;

        if (previous != muted_) {
            core::Logger::debug("WebRtcMediaTrack {} {}", id_, muted_ ? "muted" : "unmuted");
            emit<WebRtcMediaTrackEvent::MutedChanged>(muted_);
        }
    }

    /**
     * Get the track state
     */
    WebRtcMediaTrackState state() const override {
        return state_;
    }

    /**
     * Stop the track
     */
    void stop() override {
        if (state_ != WebRtcMediaTrackState::Ended) {
            state_ = WebRtcMediaTrackState::Ended;
            core::Logger::debug("WebRtcMediaTrack {} stopped", id_);
            emit<WebRtcMediaTrackEvent::StateChanged>(state_);
        }
    }

    /**
     * Clone the track
     */
    std::shared_ptr<WebRtcMediaTrack> clone() const override {
        auto cloned = std::make_shared<WebRtcMediaTrackImpl>(id_ + "_clone", type_, enabled_);
        cloned->setMuted(muted_);
        core::Logger::debug("WebRtcMediaTrack {} cloned", id_);
        return cloned;
    }

private:
    std::string id_;                 ///< Track ID
    WebRtcMediaType type_;           ///< Track type
    std::atomic<bool> enabled_;      ///< Enabled state
    std::atomic<bool> muted_;        ///< Muted state
    std::atomic<WebRtcMediaTrackState> state_; ///< Track state
    mutable std::mutex mutex_;       ///< Mutex for thread safety
};

//-------------------------------------------------------------------------------------------------
// Factory methods
//-------------------------------------------------------------------------------------------------

std::shared_ptr<WebRtcMediaTrack> WebRtcMediaTrack::create(const std::string& id,
                                                          WebRtcMediaType type,
                                                          bool enabled) {
    return std::make_shared<WebRtcMediaTrackImpl>(id, type, enabled);
}

} // namespace fmus::webrtc