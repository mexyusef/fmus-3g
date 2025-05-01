#include <fmus/rtp/rtp.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/media/media.hpp>
#include <fmus/core/task.hpp>

#include <algorithm>
#include <chrono>
#include <thread>
#include <random>

namespace fmus::rtp {

// RTP Session Implementation
class RtpSessionImpl : public RtpSession {
public:
    RtpSessionImpl()
        : stopping_(false),
          next_stream_id_(1) {
        // Membuat random generator untuk SSRC
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
        session_id_ = dist(gen);

        core::Logger::info("RTP Session created with ID: {}", session_id_);
    }

    ~RtpSessionImpl() override {
        stop();
    }

    void start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rtcp_task_) return; // Already started

        stopping_ = false;
        rtcp_task_ = core::TaskScheduler::getInstance().start([this]() {
            rtcpLoop();
            return core::TaskResult<void>::success();
        }, core::TaskPriority::Normal, "RTCP-Loop");

        core::Logger::info("RTP Session started");
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!rtcp_task_) return; // Not started

            stopping_ = true;

            // Close all streams
            for (auto& stream : streams_) {
                if (stream.second) {
                    stream.second->close();
                }
            }
        }

        // Wait for RTCP task to finish
        if (rtcp_task_) {
            rtcp_task_.await();
            rtcp_task_ = core::Task<void>();
        }

        core::Logger::info("RTP Session stopped");
    }

    uint32_t addStream(std::shared_ptr<RtpStream> stream) override {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t stream_id = next_stream_id_++;
        streams_[stream_id] = stream;
        return stream_id;
    }

    void removeStream(uint32_t stream_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            it->second->close();
            streams_.erase(it);
        }
    }

    std::shared_ptr<RtpStream> getStream(uint32_t stream_id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::vector<uint32_t> getStreamIds() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint32_t> ids;
        ids.reserve(streams_.size());
        for (const auto& pair : streams_) {
            ids.push_back(pair.first);
        }
        return ids;
    }

    uint32_t getSessionId() const override {
        return session_id_;
    }

private:
    // RTCP loop to send periodic reports
    void rtcpLoop() {
        const auto interval = std::chrono::milliseconds(5000); // 5 seconds interval
        auto next_report_time = std::chrono::steady_clock::now() + interval;

        while (!stopping_) {
            auto now = std::chrono::steady_clock::now();

            if (now >= next_report_time) {
                sendRtcpReports();
                next_report_time = now + interval;
            }

            // Sleep until next report time or some shorter interval
            auto sleep_time = std::min(next_report_time - now, std::chrono::milliseconds(100));
            if (sleep_time.count() > 0) {
                std::this_thread::sleep_for(sleep_time);
            }
        }
    }

    void sendRtcpReports() {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& pair : streams_) {
            auto& stream = pair.second;
            if (stream && stream->isActive()) {
                stream->sendRtcpReport();
            }
        }
    }

private:
    uint32_t session_id_;
    std::mutex mutex_;
    std::atomic<bool> stopping_;
    std::unordered_map<uint32_t, std::shared_ptr<RtpStream>> streams_;
    uint32_t next_stream_id_;
    core::Task<void> rtcp_task_;
};

// RTP Stream Base Implementation
class RtpStreamBase : public RtpStream {
public:
    RtpStreamBase(uint32_t ssrc, const std::string& cname)
        : ssrc_(ssrc),
          cname_(cname),
          sequence_number_(0),
          timestamp_(0),
          active_(false),
          rtcp_rr_received_(0),
          rtcp_sr_received_(0),
          packets_sent_(0),
          bytes_sent_(0) {
    }

    ~RtpStreamBase() override {
        close();
    }

    uint32_t getSsrc() const override {
        return ssrc_;
    }

    const std::string& getCname() const override {
        return cname_;
    }

    bool isActive() const override {
        return active_;
    }

    void close() override {
        active_ = false;
    }

    void sendRtcpReport() override {
        if (!active_) return;

        auto now = std::chrono::system_clock::now();
        auto ntp_time = toNtpTime(now);

        RtcpSenderReport sr;
        sr.setSsrc(ssrc_);
        sr.setNtpTimestamp(ntp_time.first, ntp_time.second);
        sr.setRtpTimestamp(timestamp_);
        sr.setPacketCount(packets_sent_);
        sr.setOctetCount(bytes_sent_);

        auto data = sr.serialize();
        sendRtcpPacket(data.data(), data.size());
    }

    void processRtcpPacket(const uint8_t* data, size_t size) override {
        try {
            auto packet = RtcpPacket::deserialize(data, size);

            if (auto sr = dynamic_cast<RtcpSenderReport*>(packet.get())) {
                rtcp_sr_received_++;
                processSenderReport(*sr);
            } else if (auto rr = dynamic_cast<RtcpReceiverReport*>(packet.get())) {
                rtcp_rr_received_++;
                processReceiverReport(*rr);
            }
        } catch (const RtpError& e) {
            core::Logger::error("Failed to process RTCP packet: {}", e.what());
        }
    }

    uint16_t getNextSequenceNumber() {
        return sequence_number_++;
    }

    uint32_t getCurrentTimestamp() const {
        return timestamp_;
    }

    void updateTimestamp(uint32_t timestamp_delta) {
        timestamp_ += timestamp_delta;
    }

protected:
    virtual void sendRtcpPacket(const uint8_t* data, size_t size) = 0;

    void processSenderReport(const RtcpSenderReport& sr) {
        // Store sender report data for jitter calculation, etc.
        last_sr_timestamp_ = sr.getRtpTimestamp();
        last_sr_ntp_high_ = sr.getNtpTimestampHigh();
        last_sr_ntp_low_ = sr.getNtpTimestampLow();
    }

    void processReceiverReport(const RtcpReceiverReport& rr) {
        // Process reception statistics and adapt if needed
        for (const auto& block : rr.getReportBlocks()) {
            if (block.ssrc == ssrc_) {
                // This is report about our stream
                fraction_lost_ = block.fractionLost;
                cumulative_lost_ = block.cumulativeLost;
                jitter_ = block.interarrivalJitter;

                // Log reception statistics
                core::Logger::debug("RTP Stream {}: Loss {}%, Jitter {}",
                    ssrc_, (fraction_lost_ * 100) / 256, jitter_);

                // Trigger adaptation if needed
                if (fraction_lost_ > 20) { // More than ~8% loss
                    onHighPacketLoss(fraction_lost_);
                }
            }
        }
    }

    virtual void onHighPacketLoss(uint8_t fraction_lost) {
        // Default implementation does nothing
        // Derived classes should override to implement adaptation
    }

    // Convert system time to NTP timestamp (64-bit, split into two 32-bit values)
    std::pair<uint32_t, uint32_t> toNtpTime(std::chrono::system_clock::time_point time) {
        // NTP time is represented as a 64-bit unsigned fixed-point number
        // The integer part represents seconds since Jan 1, 1900
        // The fractional part represents fractions of a second

        // Convert system time to seconds and nanoseconds
        auto duration = time.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);

        // Add offset between Unix epoch (Jan 1, 1970) and NTP epoch (Jan 1, 1900)
        // This is 70 years plus 17 leap days = 2208988800 seconds
        constexpr uint32_t NTP_OFFSET = 2208988800UL;
        uint32_t ntp_seconds = static_cast<uint32_t>(seconds.count() + NTP_OFFSET);

        // Convert nanoseconds to NTP fractional part (0..2^32-1)
        uint32_t ntp_fraction = static_cast<uint32_t>((nanos.count() * 0x100000000ULL) / 1000000000ULL);

        return {ntp_seconds, ntp_fraction};
    }

protected:
    uint32_t ssrc_;
    std::string cname_;
    std::atomic<uint16_t> sequence_number_;
    std::atomic<uint32_t> timestamp_;
    std::atomic<bool> active_;

    // RTCP statistics
    std::atomic<uint32_t> rtcp_rr_received_;
    std::atomic<uint32_t> rtcp_sr_received_;
    std::atomic<uint32_t> packets_sent_;
    std::atomic<uint32_t> bytes_sent_;

    // Last SR data
    uint32_t last_sr_timestamp_ = 0;
    uint32_t last_sr_ntp_high_ = 0;
    uint32_t last_sr_ntp_low_ = 0;

    // Receiver report data
    uint8_t fraction_lost_ = 0;
    uint32_t cumulative_lost_ = 0;
    uint32_t jitter_ = 0;
};

// RTP Audio Stream Implementation
class RtpAudioStreamImpl : public RtpStreamBase, public RtpAudioStream {
public:
    RtpAudioStreamImpl(uint32_t ssrc, const std::string& cname, const RtpAudioParams& params)
        : RtpStreamBase(ssrc, cname),
          params_(params) {
    }

    ~RtpAudioStreamImpl() override {
        close();
    }

    void open() override {
        if (active_) return;

        // Setup network connection based on parameters
        setupNetworkConnection();

        active_ = true;
        core::Logger::info("RTP Audio Stream {} opened with PT {}",
            ssrc_, static_cast<int>(params_.payloadType));
    }

    void close() override {
        if (!active_) return;

        active_ = false;
        closeNetworkConnection();

        core::Logger::info("RTP Audio Stream {} closed", ssrc_);
    }

    bool sendFrame(const media::AudioFrame& frame) override {
        if (!active_) return false;

        // Convert audio frame to RTP packets and send them
        uint32_t samples_per_packet = params_.packetizationTime * params_.clockRate / 1000;
        uint32_t bytes_per_sample = frame.getChannels() * 2; // Assuming 16-bit samples
        uint32_t bytes_per_packet = samples_per_packet * bytes_per_sample;

        // Get the audio data
        const uint8_t* data = frame.getData();
        size_t remaining = frame.getSize();
        uint32_t timestamp_delta = frame.getSamples();

        while (remaining > 0) {
            // Create RTP packet
            RtpHeader header;
            header.version = 2;
            header.padding = false;
            header.extension = false;
            header.csrcCount = 0;
            header.marker = (remaining == frame.getSize()); // Mark first packet
            header.payloadType = static_cast<uint8_t>(params_.payloadType);
            header.sequenceNumber = getNextSequenceNumber();
            header.timestamp = getCurrentTimestamp();
            header.ssrc = ssrc_;

            // Calculate payload size for this packet
            size_t payload_size = std::min(static_cast<size_t>(bytes_per_packet), remaining);

            // Create the packet
            RtpPacket packet(header, data, payload_size);

            // Send the packet
            auto serialized = packet.serialize();
            if (!sendRtpPacket(serialized.data(), serialized.size())) {
                return false;
            }

            // Update counters
            data += payload_size;
            remaining -= payload_size;
            packets_sent_++;
            bytes_sent_ += payload_size;
        }

        // Update timestamp for next frame
        updateTimestamp(timestamp_delta);

        return true;
    }

    RtpAudioParams getParams() const override {
        return params_;
    }

protected:
    // Placeholder for network connection setup
    void setupNetworkConnection() {
        // In a real implementation, this would create UDP sockets, etc.
    }

    void closeNetworkConnection() {
        // Close UDP sockets, etc.
    }

    bool sendRtpPacket(const uint8_t* data, size_t size) {
        // In a real implementation, this would send the packet over UDP
        return true;
    }

    void sendRtcpPacket(const uint8_t* data, size_t size) override {
        // In a real implementation, this would send the RTCP packet over UDP
    }

    void onHighPacketLoss(uint8_t fraction_lost) override {
        // Could implement adaptive bitrate by changing codec parameters
        core::Logger::warning("RTP Audio Stream {}: High packet loss detected: {}%",
            ssrc_, (fraction_lost * 100) / 256);
    }

private:
    RtpAudioParams params_;
};

// RTP Video Stream Implementation
class RtpVideoStreamImpl : public RtpStreamBase, public RtpVideoStream {
public:
    RtpVideoStreamImpl(uint32_t ssrc, const std::string& cname, const RtpVideoParams& params)
        : RtpStreamBase(ssrc, cname),
          params_(params) {
    }

    ~RtpVideoStreamImpl() override {
        close();
    }

    void open() override {
        if (active_) return;

        // Setup network connection based on parameters
        setupNetworkConnection();

        active_ = true;
        core::Logger::info("RTP Video Stream {} opened with PT {}",
            ssrc_, static_cast<int>(params_.payloadType));
    }

    void close() override {
        if (!active_) return;

        active_ = false;
        closeNetworkConnection();

        core::Logger::info("RTP Video Stream {} closed", ssrc_);
    }

    bool sendFrame(const media::VideoFrame& frame) override {
        if (!active_) return false;

        // For video, we need more complex packetization based on the codec
        // This is a simplified version

        // Get the video data
        const uint8_t* data = frame.getData();
        size_t remaining = frame.getSize();

        // For simplicity, assume 90kHz clock rate as per most video RTP specs
        uint32_t timestamp_delta = 90000 / frame.getFrameRate();

        // Calculate MTU-compatible chunk size
        size_t max_payload_size = params_.maxPacketSize - 40; // IP + UDP + RTP headers

        bool first_packet = true;
        bool last_packet = false;

        while (remaining > 0) {
            // Create RTP packet
            RtpHeader header;
            header.version = 2;
            header.padding = false;
            header.extension = false;
            header.csrcCount = 0;

            // Set marker bit on last packet of the frame
            last_packet = (remaining <= max_payload_size);
            header.marker = last_packet;

            header.payloadType = static_cast<uint8_t>(params_.payloadType);
            header.sequenceNumber = getNextSequenceNumber();
            header.timestamp = getCurrentTimestamp();
            header.ssrc = ssrc_;

            // Calculate payload size for this packet
            size_t payload_size = std::min(max_payload_size, remaining);

            // Create the packet
            RtpPacket packet(header, data, payload_size);

            // Send the packet
            auto serialized = packet.serialize();
            if (!sendRtpPacket(serialized.data(), serialized.size())) {
                return false;
            }

            // Update counters
            data += payload_size;
            remaining -= payload_size;
            packets_sent_++;
            bytes_sent_ += payload_size;

            first_packet = false;
        }

        // Update timestamp for next frame
        updateTimestamp(timestamp_delta);

        return true;
    }

    RtpVideoParams getParams() const override {
        return params_;
    }

protected:
    // Placeholder for network connection setup
    void setupNetworkConnection() {
        // In a real implementation, this would create UDP sockets, etc.
    }

    void closeNetworkConnection() {
        // Close UDP sockets, etc.
    }

    bool sendRtpPacket(const uint8_t* data, size_t size) {
        // In a real implementation, this would send the packet over UDP
        return true;
    }

    void sendRtcpPacket(const uint8_t* data, size_t size) override {
        // In a real implementation, this would send the RTCP packet over UDP
    }

    void onHighPacketLoss(uint8_t fraction_lost) override {
        // Could implement adaptive bitrate
        core::Logger::warning("RTP Video Stream {}: High packet loss detected: {}%, adapting...",
            ssrc_, (fraction_lost * 100) / 256);
    }

private:
    RtpVideoParams params_;
};

// Factory methods
std::shared_ptr<RtpSession> RtpSession::create() {
    return std::make_shared<RtpSessionImpl>();
}

std::shared_ptr<RtpAudioStream> RtpAudioStream::create(uint32_t ssrc,
                                                      const std::string& cname,
                                                      const RtpAudioParams& params) {
    return std::make_shared<RtpAudioStreamImpl>(ssrc, cname, params);
}

std::shared_ptr<RtpVideoStream> RtpVideoStream::create(uint32_t ssrc,
                                                      const std::string& cname,
                                                      const RtpVideoParams& params) {
    return std::make_shared<RtpVideoStreamImpl>(ssrc, cname, params);
}

} // namespace fmus::rtp