#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>

#include <memory>
#include <queue>
#include <mutex>

namespace fmus::webrtc {

// WebRTC data channel implementation
class WebRtcDataChannelImpl : public WebRtcDataChannel,
                            public std::enable_shared_from_this<WebRtcDataChannelImpl> {
public:
    WebRtcDataChannelImpl(const std::string& label, const WebRtcDataChannelInit& init)
        : label_(label),
          ordered_(init.ordered),
          max_packet_life_time_(init.max_packet_life_time),
          max_retransmits_(init.max_retransmits),
          protocol_(init.protocol),
          negotiated_(init.negotiated),
          id_(init.id ? *init.id : generateChannelId()),
          state_(WebRtcDataChannelState::Connecting) {

        core::Logger::info("WebRtcDataChannelImpl created: {}", label);
    }

    ~WebRtcDataChannelImpl() {
        close();
        core::Logger::info("WebRtcDataChannelImpl destroyed: {}", label_);
    }

    // Properties
    std::string label() const override {
        return label_;
    }

    bool ordered() const override {
        return ordered_;
    }

    std::optional<int> maxPacketLifeTime() const override {
        return max_packet_life_time_;
    }

    std::optional<int> maxRetransmits() const override {
        return max_retransmits_;
    }

    std::string protocol() const override {
        return protocol_;
    }

    bool negotiated() const override {
        return negotiated_;
    }

    int id() const override {
        return id_;
    }

    uint64_t bufferedAmount() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffered_amount_;
    }

    WebRtcDataChannelState state() const override {
        return state_;
    }

    // Methods
    void send(const std::string& data) override {
        if (state_ != WebRtcDataChannelState::Open) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Data channel is not open");
        }

        // Mensimulasikan mengirim data
        core::Logger::info("Sending text data on channel {}: {} bytes",
                         label_, data.size());

        // Update buffered amount
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffered_amount_ += data.size();
        }

        // Simulate data being sent
        simulateSendComplete(data.size());
    }

    void send(const uint8_t* data, size_t size) override {
        if (state_ != WebRtcDataChannelState::Open) {
            throw WebRtcError(WebRtcErrorCode::InvalidState,
                           "Data channel is not open");
        }

        // Mensimulasikan mengirim data biner
        core::Logger::info("Sending binary data on channel {}: {} bytes",
                         label_, size);

        // Update buffered amount
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffered_amount_ += size;
        }

        // Simulate data being sent
        simulateSendComplete(size);
    }

    void close() override {
        // Memeriksa apakah channel sudah closed
        if (state_ == WebRtcDataChannelState::Closing ||
            state_ == WebRtcDataChannelState::Closed) {
            return;
        }

        // Update state to closing
        setState(WebRtcDataChannelState::Closing);

        // Simulate closing delay
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            setState(WebRtcDataChannelState::Closed);
        }).detach();
    }

    // Simulate channel being connected
    void simulateOpen() {
        if (state_ == WebRtcDataChannelState::Connecting) {
            setState(WebRtcDataChannelState::Open);
        }
    }

    // Simulate receiving a text message
    void simulateMessage(const std::string& data) {
        if (state_ == WebRtcDataChannelState::Open) {
            onMessage.emit(data);
        }
    }

    // Simulate receiving a binary message
    void simulateMessage(const std::vector<uint8_t>& data) {
        if (state_ == WebRtcDataChannelState::Open) {
            onBinaryMessage.emit(data);
        }
    }

private:
    // Update state and emit event
    void setState(WebRtcDataChannelState new_state) {
        if (state_ != new_state) {
            state_ = new_state;
            onStateChange.emit(state_);
        }
    }

    // Simulate send completion
    void simulateSendComplete(size_t size) {
        std::thread([this, size]() {
            // Simulate network delay
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            // Update buffered amount
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (buffered_amount_ >= size) {
                    buffered_amount_ -= size;
                } else {
                    buffered_amount_ = 0;
                }
            }

            // Emit buffered amount change event
            onBufferedAmountChange.emit(buffered_amount_);
        }).detach();
    }

    // Generate unique channel ID
    static int generateChannelId() {
        static std::atomic<int> next_id = 0;
        return next_id++;
    }

    // Properties
    std::string label_;
    bool ordered_;
    std::optional<int> max_packet_life_time_;
    std::optional<int> max_retransmits_;
    std::string protocol_;
    bool negotiated_;
    int id_;

    // State
    WebRtcDataChannelState state_;
    uint64_t buffered_amount_ = 0;

    // Mutex for thread safety
    mutable std::mutex mutex_;
};

} // namespace fmus::webrtc