#include <fmus/rtp/rtp.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>
#include <fmus/media/media.hpp>

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <chrono>
#include <random>

namespace fmus::rtp {

// RTP Sender Implementation
class RtpSenderImpl : public RtpSender {
public:
    RtpSenderImpl() {
        // Membuat random generator untuk SSRC
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
        default_ssrc_ = dist(gen);

        session_ = RtpSession::create();

        core::Logger::info("RTP Sender created with default SSRC: {}", default_ssrc_);
    }

    ~RtpSenderImpl() override {
        stop();
    }

    void start() override {
        session_->start();
        is_active_ = true;
        core::Logger::info("RTP Sender started");
    }

    void stop() override {
        is_active_ = false;
        session_->stop();
        core::Logger::info("RTP Sender stopped");
    }

    std::shared_ptr<RtpAudioStream> createAudioStream(const RtpAudioParams& params,
                                                     const std::string& cname) override {
        if (!is_active_) {
            core::Logger::error("Cannot create audio stream: RTP Sender not started");
            return nullptr;
        }

        // Generate SSRC for this stream
        uint32_t ssrc = generateSsrc();

        // Create the stream
        auto stream = RtpAudioStream::create(ssrc, cname.empty() ? default_cname_ : cname, params);

        // Add it to the session
        uint32_t stream_id = session_->addStream(stream);
        stream_ids_[ssrc] = stream_id;

        // Open the stream
        stream->open();

        core::Logger::info("Created RTP Audio Stream with SSRC: {}", ssrc);
        return stream;
    }

    std::shared_ptr<RtpVideoStream> createVideoStream(const RtpVideoParams& params,
                                                     const std::string& cname) override {
        if (!is_active_) {
            core::Logger::error("Cannot create video stream: RTP Sender not started");
            return nullptr;
        }

        // Generate SSRC for this stream
        uint32_t ssrc = generateSsrc();

        // Create the stream
        auto stream = RtpVideoStream::create(ssrc, cname.empty() ? default_cname_ : cname, params);

        // Add it to the session
        uint32_t stream_id = session_->addStream(stream);
        stream_ids_[ssrc] = stream_id;

        // Open the stream
        stream->open();

        core::Logger::info("Created RTP Video Stream with SSRC: {}", ssrc);
        return stream;
    }

    bool sendAudioFrame(std::shared_ptr<RtpAudioStream> stream,
                       const media::AudioFrame& frame) override {
        if (!is_active_) {
            core::Logger::error("Cannot send audio frame: RTP Sender not started");
            return false;
        }

        return stream->sendFrame(frame);
    }

    bool sendVideoFrame(std::shared_ptr<RtpVideoStream> stream,
                       const media::VideoFrame& frame) override {
        if (!is_active_) {
            core::Logger::error("Cannot send video frame: RTP Sender not started");
            return false;
        }

        return stream->sendFrame(frame);
    }

    void setCname(const std::string& cname) override {
        default_cname_ = cname;
    }

    std::string getCname() const override {
        return default_cname_;
    }

private:
    uint32_t generateSsrc() {
        // Generate a new SSRC that's different from any existing ones
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);

        uint32_t ssrc;
        do {
            ssrc = dist(gen);
            // Check if it's different from default and existing SSRCs
        } while (ssrc == default_ssrc_ || stream_ids_.find(ssrc) != stream_ids_.end());

        return ssrc;
    }

private:
    uint32_t default_ssrc_;
    std::string default_cname_ = "fmus-rtp-sender";
    std::shared_ptr<RtpSession> session_;
    std::atomic<bool> is_active_ = false;
    std::unordered_map<uint32_t, uint32_t> stream_ids_; // SSRC -> stream_id
};

// RTP Receiver Implementation
class RtpReceiverImpl : public RtpReceiver {
public:
    RtpReceiverImpl() {
        session_ = RtpSession::create();
        core::Logger::info("RTP Receiver created");
    }

    ~RtpReceiverImpl() override {
        stop();
    }

    void start() override {
        session_->start();

        // Start packet receiving task
        stopping_ = false;
        receive_task_ = core::TaskScheduler::getInstance().start([this]() {
            receiveLoop();
            return core::TaskResult<void>::success();
        }, core::TaskPriority::High, "RTP-Receive");

        is_active_ = true;
        core::Logger::info("RTP Receiver started");
    }

    void stop() override {
        if (!is_active_) return;

        is_active_ = false;
        stopping_ = true;

        // Wait for receive task to finish
        if (receive_task_) {
            receive_task_.await();
            receive_task_ = core::Task<void>();
        }

        session_->stop();
        core::Logger::info("RTP Receiver stopped");
    }

    void addAudioTrackHandler(const std::string& media_id,
                             std::function<void(media::AudioFrame)> handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_handlers_[media_id] = std::move(handler);
    }

    void addVideoTrackHandler(const std::string& media_id,
                             std::function<void(media::VideoFrame)> handler) override {
        std::lock_guard<std::mutex> lock(mutex_);
        video_handlers_[media_id] = std::move(handler);
    }

    void removeTrackHandler(const std::string& media_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_handlers_.erase(media_id);
        video_handlers_.erase(media_id);
    }

    void setRtpPort(uint16_t port) override {
        rtp_port_ = port;
    }

    void setRtcpPort(uint16_t port) override {
        rtcp_port_ = port;
    }

    uint16_t getRtpPort() const override {
        return rtp_port_;
    }

    uint16_t getRtcpPort() const override {
        return rtcp_port_;
    }

private:
    // Simulated network packet receive loop
    void receiveLoop() {
        const auto check_interval = std::chrono::milliseconds(10);

        while (!stopping_) {
            // In a real implementation, this would wait for UDP packets
            // and process them when they arrive

            // For this mock implementation, we'll just sleep and check if we should stop
            std::this_thread::sleep_for(check_interval);
        }
    }

    // Process an incoming RTP packet
    void processRtpPacket(const uint8_t* data, size_t size) {
        try {
            // Deserialize the packet
            RtpPacket packet = RtpPacket::deserialize(data, size);

            // Get the payload type to determine if it's audio or video
            uint8_t payload_type = packet.getHeader().payloadType;
            uint32_t ssrc = packet.getHeader().ssrc;

            // Find the track by SSRC
            std::lock_guard<std::mutex> lock(mutex_);
            auto track_it = tracks_.find(ssrc);
            if (track_it == tracks_.end()) {
                // New track, create it
                std::string media_id = "track-" + std::to_string(ssrc);
                tracks_[ssrc] = media_id;

                // Determine if it's audio or video based on payload type
                if (isAudioPayloadType(payload_type)) {
                    core::Logger::info("New audio track detected: SSRC={}, PT={}", ssrc, payload_type);
                    processNewAudioTrack(ssrc, payload_type, media_id);
                } else {
                    core::Logger::info("New video track detected: SSRC={}, PT={}", ssrc, payload_type);
                    processNewVideoTrack(ssrc, payload_type, media_id);
                }

                track_it = tracks_.find(ssrc);
            }

            if (track_it != tracks_.end()) {
                std::string media_id = track_it->second;

                // Process the packet based on its payload type
                if (isAudioPayloadType(payload_type)) {
                    processAudioPacket(packet, media_id);
                } else {
                    processVideoPacket(packet, media_id);
                }
            }
        } catch (const RtpError& e) {
            core::Logger::error("Failed to process RTP packet: {}", e.what());
        }
    }

    // Process an incoming RTCP packet
    void processRtcpPacket(const uint8_t* data, size_t size) {
        try {
            auto packet = RtcpPacket::deserialize(data, size);

            // Handle different RTCP packet types
            if (auto sr = dynamic_cast<RtcpSenderReport*>(packet.get())) {
                uint32_t ssrc = sr->getSsrc();

                // Find the stream and forward the SR
                auto stream = findStreamBySsrc(ssrc);
                if (stream) {
                    stream->processRtcpPacket(data, size);
                }
            } else if (auto rr = dynamic_cast<RtcpReceiverReport*>(packet.get())) {
                uint32_t ssrc = rr->getSsrc();

                // Find the stream and forward the RR
                auto stream = findStreamBySsrc(ssrc);
                if (stream) {
                    stream->processRtcpPacket(data, size);
                }
            }
        } catch (const RtpError& e) {
            core::Logger::error("Failed to process RTCP packet: {}", e.what());
        }
    }

    void processNewAudioTrack(uint32_t ssrc, uint8_t payload_type, const std::string& media_id) {
        // Create RTP parameters based on the payload type
        RtpAudioParams params;
        params.payloadType = static_cast<RtpPayloadType>(payload_type);

        // Set default values for the parameters based on payload type
        switch (payload_type) {
            case static_cast<uint8_t>(RtpPayloadType::PCMU):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::GSM):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::G723):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 30;
                break;
            case static_cast<uint8_t>(RtpPayloadType::DVI4_8K):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::DVI4_16K):
                params.clockRate = 16000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::LPC):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::PCMA):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::G722):
                params.clockRate = 8000;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::L16_2CH):
                params.clockRate = 44100;
                params.channels = 2;
                params.packetizationTime = 20;
                break;
            case static_cast<uint8_t>(RtpPayloadType::L16_1CH):
                params.clockRate = 44100;
                params.channels = 1;
                params.packetizationTime = 20;
                break;
            default:
                // Default parameters for unknown payload types
                params.clockRate = 48000;
                params.channels = 2;
                params.packetizationTime = 20;
                break;
        }

        // Create and store the stream
        auto stream = RtpAudioStream::create(ssrc, "fmus-receiver", params);
        uint32_t stream_id = session_->addStream(stream);

        // Store mapping
        ssrc_to_stream_id_[ssrc] = stream_id;
    }

    void processNewVideoTrack(uint32_t ssrc, uint8_t payload_type, const std::string& media_id) {
        // Create RTP parameters based on the payload type
        RtpVideoParams params;
        params.payloadType = static_cast<RtpPayloadType>(payload_type);

        // Set default values for the parameters based on payload type
        switch (payload_type) {
            case static_cast<uint8_t>(RtpPayloadType::MJPEG):
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
            case static_cast<uint8_t>(RtpPayloadType::H261):
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
            case static_cast<uint8_t>(RtpPayloadType::MPV):
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
            case static_cast<uint8_t>(RtpPayloadType::MP2T):
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
            case static_cast<uint8_t>(RtpPayloadType::H263):
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
            default:
                // Default parameters for unknown payload types
                params.clockRate = 90000;
                params.maxPacketSize = 1400;
                break;
        }

        // Create and store the stream
        auto stream = RtpVideoStream::create(ssrc, "fmus-receiver", params);
        uint32_t stream_id = session_->addStream(stream);

        // Store mapping
        ssrc_to_stream_id_[ssrc] = stream_id;
    }

    void processAudioPacket(const RtpPacket& packet, const std::string& media_id) {
        // Find the handler for this media_id
        auto handler_it = audio_handlers_.find(media_id);
        if (handler_it == audio_handlers_.end()) return;

        const auto& handler = handler_it->second;
        if (!handler) return;

        // Get packet info
        const RtpHeader& header = packet.getHeader();
        const uint8_t* payload = packet.getPayload();
        size_t payload_size = packet.getPayloadSize();

        // Create audio frame from packet
        // This is simplified and would depend on the codec in a real implementation
        media::AudioFrameFormat format = media::AudioFrameFormat::PCM_S16LE;
        uint32_t sample_rate = 48000;
        uint8_t channels = 2;

        // Find the stream to get proper parameters
        auto stream_id_it = ssrc_to_stream_id_.find(header.ssrc);
        if (stream_id_it != ssrc_to_stream_id_.end()) {
            auto stream = session_->getStream(stream_id_it->second);
            if (auto audio_stream = std::dynamic_pointer_cast<RtpAudioStream>(stream)) {
                const auto& params = audio_stream->getParams();
                sample_rate = params.clockRate;
                channels = params.channels;
                // Map payload type to audio format
                format = mapPayloadTypeToAudioFormat(static_cast<uint8_t>(params.payloadType));
            }
        }

        try {
            // Create audio frame from packet
            media::AudioFrame frame(
                format,
                sample_rate,
                channels,
                payload,
                payload_size
            );

            // Call the handler with the frame
            handler(frame);
        } catch (const std::exception& e) {
            core::Logger::error("Failed to create audio frame from RTP packet: {}", e.what());
        }
    }

    void processVideoPacket(const RtpPacket& packet, const std::string& media_id) {
        // Find the handler for this media_id
        auto handler_it = video_handlers_.find(media_id);
        if (handler_it == video_handlers_.end()) return;

        const auto& handler = handler_it->second;
        if (!handler) return;

        const RtpHeader& header = packet.getHeader();
        const uint8_t* payload = packet.getPayload();
        size_t payload_size = packet.getPayloadSize();

        // Default video parameters
        media::VideoFrameFormat format = media::VideoFrameFormat::H264;
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t frame_rate = 30;

        // Find the stream to get proper parameters
        auto stream_id_it = ssrc_to_stream_id_.find(header.ssrc);
        if (stream_id_it != ssrc_to_stream_id_.end()) {
            auto stream = session_->getStream(stream_id_it->second);
            if (auto video_stream = std::dynamic_pointer_cast<RtpVideoStream>(stream)) {
                const auto& params = video_stream->getParams();
                // Map payload type to video format
                format = mapPayloadTypeToVideoFormat(static_cast<uint8_t>(params.payloadType));
            }
        }

        // For video, we need to accumulate packets until we have a complete frame
        // This is indicated by the marker bit in the RTP header

        // If this is the first packet of a frame or we don't have a buffer for this SSRC
        if (video_frame_buffers_.find(header.ssrc) == video_frame_buffers_.end() ||
            (header.marker && video_frame_buffers_[header.ssrc].empty())) {
            video_frame_buffers_[header.ssrc].clear();
        }

        // Add this packet to the buffer
        auto& buffer = video_frame_buffers_[header.ssrc];
        size_t current_size = buffer.size();
        buffer.resize(current_size + payload_size);
        std::memcpy(buffer.data() + current_size, payload, payload_size);

        // If this is the last packet of a frame, create a video frame and deliver it
        if (header.marker) {
            try {
                // Create video frame from accumulated packets
                media::VideoFrame frame(
                    format,
                    width,
                    height,
                    frame_rate,
                    buffer.data(),
                    buffer.size()
                );

                // Call the handler with the frame
                handler(frame);

                // Clear the buffer for this SSRC
                buffer.clear();
            } catch (const std::exception& e) {
                core::Logger::error("Failed to create video frame from RTP packets: {}", e.what());
            }
        }
    }

    std::shared_ptr<RtpStream> findStreamBySsrc(uint32_t ssrc) {
        auto it = ssrc_to_stream_id_.find(ssrc);
        if (it != ssrc_to_stream_id_.end()) {
            return session_->getStream(it->second);
        }
        return nullptr;
    }

    bool isAudioPayloadType(uint8_t pt) {
        // Check if the payload type is for audio based on RFC 3551
        static const std::unordered_set<uint8_t> audio_pts = {
            static_cast<uint8_t>(RtpPayloadType::PCMU),
            static_cast<uint8_t>(RtpPayloadType::GSM),
            static_cast<uint8_t>(RtpPayloadType::G723),
            static_cast<uint8_t>(RtpPayloadType::DVI4_8K),
            static_cast<uint8_t>(RtpPayloadType::DVI4_16K),
            static_cast<uint8_t>(RtpPayloadType::LPC),
            static_cast<uint8_t>(RtpPayloadType::PCMA),
            static_cast<uint8_t>(RtpPayloadType::G722),
            static_cast<uint8_t>(RtpPayloadType::L16_2CH),
            static_cast<uint8_t>(RtpPayloadType::L16_1CH),
            static_cast<uint8_t>(RtpPayloadType::QCELP),
            static_cast<uint8_t>(RtpPayloadType::CN),
            static_cast<uint8_t>(RtpPayloadType::MPA),
            static_cast<uint8_t>(RtpPayloadType::G728),
            static_cast<uint8_t>(RtpPayloadType::DVI4_11K),
            static_cast<uint8_t>(RtpPayloadType::DVI4_22K),
            static_cast<uint8_t>(RtpPayloadType::G729)
        };

        return audio_pts.find(pt) != audio_pts.end();
    }

    media::AudioFrameFormat mapPayloadTypeToAudioFormat(uint8_t pt) {
        // Map RTP payload type to audio format
        switch (pt) {
            case static_cast<uint8_t>(RtpPayloadType::PCMU):
                return media::AudioFrameFormat::PCM_U8;
            case static_cast<uint8_t>(RtpPayloadType::PCMA):
                return media::AudioFrameFormat::PCM_ALAW;
            case static_cast<uint8_t>(RtpPayloadType::G722):
                return media::AudioFrameFormat::G722;
            case static_cast<uint8_t>(RtpPayloadType::L16_1CH):
            case static_cast<uint8_t>(RtpPayloadType::L16_2CH):
                return media::AudioFrameFormat::PCM_S16BE;
            default:
                return media::AudioFrameFormat::PCM_S16LE;
        }
    }

    media::VideoFrameFormat mapPayloadTypeToVideoFormat(uint8_t pt) {
        // Map RTP payload type to video format
        switch (pt) {
            case static_cast<uint8_t>(RtpPayloadType::MJPEG):
                return media::VideoFrameFormat::MJPEG;
            case static_cast<uint8_t>(RtpPayloadType::H261):
                return media::VideoFrameFormat::H261;
            case static_cast<uint8_t>(RtpPayloadType::MPV):
                return media::VideoFrameFormat::MPEG1;
            case static_cast<uint8_t>(RtpPayloadType::MP2T):
                return media::VideoFrameFormat::MPEG2TS;
            case static_cast<uint8_t>(RtpPayloadType::H263):
                return media::VideoFrameFormat::H263;
            default:
                return media::VideoFrameFormat::H264;
        }
    }

private:
    std::shared_ptr<RtpSession> session_;
    std::atomic<bool> is_active_ = false;
    std::atomic<bool> stopping_ = false;
    core::Task<void> receive_task_;

    std::mutex mutex_;
    std::unordered_map<std::string, std::function<void(media::AudioFrame)>> audio_handlers_;
    std::unordered_map<std::string, std::function<void(media::VideoFrame)>> video_handlers_;

    std::unordered_map<uint32_t, std::string> tracks_; // SSRC -> media_id
    std::unordered_map<uint32_t, uint32_t> ssrc_to_stream_id_; // SSRC -> stream_id
    std::unordered_map<uint32_t, std::vector<uint8_t>> video_frame_buffers_; // SSRC -> accumulated frame data

    uint16_t rtp_port_ = 10000;
    uint16_t rtcp_port_ = 10001;
};

// Factory methods
std::shared_ptr<RtpSender> RtpSender::create() {
    return std::make_shared<RtpSenderImpl>();
}

std::shared_ptr<RtpReceiver> RtpReceiver::create() {
    return std::make_shared<RtpReceiverImpl>();
}

} // namespace fmus::rtp