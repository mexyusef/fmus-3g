#include <fmus/media/media.hpp>
#include <fmus/core/logger.hpp>

#include <cassert>
#include <cstring>
#include <algorithm>

namespace fmus::media {

// MediaError implementation
MediaError::MediaError(MediaErrorCode code, const std::string& message)
    : Error(core::ErrorCode::MediaError, message), code_(code) {}

// AudioFrame implementation
AudioFrame::AudioFrame(AudioSampleFormat format, uint32_t sample_rate,
                       uint8_t channels, size_t num_samples)
    : format_(format), sample_rate_(sample_rate), channels_(channels),
      num_samples_(num_samples) {

    // Hitung ukuran data berdasarkan format
    size_t bytes_per_sample = 0;
    switch (format) {
        case AudioSampleFormat::S16:
            bytes_per_sample = 2;
            break;
        case AudioSampleFormat::S32:
        case AudioSampleFormat::F32:
            bytes_per_sample = 4;
            break;
        default:
            throw MediaError(MediaErrorCode::FormatNotSupported,
                "Unsupported audio format");
    }

    // Alokasi memory untuk data
    data_.resize(num_samples * channels * bytes_per_sample);
}

AudioFrame::AudioFrame(const void* data, size_t size, AudioSampleFormat format,
                       uint32_t sample_rate, uint8_t channels)
    : format_(format), sample_rate_(sample_rate), channels_(channels) {

    // Hitung bytes per sample
    size_t bytes_per_sample = 0;
    switch (format) {
        case AudioSampleFormat::S16:
            bytes_per_sample = 2;
            break;
        case AudioSampleFormat::S32:
        case AudioSampleFormat::F32:
            bytes_per_sample = 4;
            break;
        default:
            throw MediaError(MediaErrorCode::FormatNotSupported,
                "Unsupported audio format");
    }

    // Hitung jumlah sample
    if (size % (channels * bytes_per_sample) != 0) {
        throw MediaError(MediaErrorCode::InvalidParameter,
            "Invalid audio data size");
    }

    num_samples_ = size / (channels * bytes_per_sample);

    // Copy data
    data_.resize(size);
    std::memcpy(data_.data(), data, size);
}

std::chrono::microseconds AudioFrame::duration() const {
    if (sample_rate_ == 0) {
        return std::chrono::microseconds(0);
    }

    // Durasi = jumlah sample / sample rate (dalam mikrodetik)
    return std::chrono::microseconds(
        static_cast<uint64_t>(1000000ULL * num_samples_ / sample_rate_));
}

std::unique_ptr<AudioFrame> AudioFrame::clone() const {
    auto clone = std::make_unique<AudioFrame>();
    clone->data_ = data_;
    clone->format_ = format_;
    clone->sample_rate_ = sample_rate_;
    clone->channels_ = channels_;
    clone->num_samples_ = num_samples_;
    return clone;
}

// VideoFrame implementation
VideoFrame::VideoFrame(VideoPixelFormat format, uint32_t width, uint32_t height)
    : format_(format), width_(width), height_(height) {

    // Hitung ukuran data berdasarkan format dan resolusi
    size_t frame_size = 0;
    switch (format) {
        case VideoPixelFormat::RGB24:
            frame_size = width * height * 3;
            break;
        case VideoPixelFormat::RGBA32:
            frame_size = width * height * 4;
            break;
        case VideoPixelFormat::YUV420P:
            // Y plane = width * height
            // U dan V planes = (width/2) * (height/2) each
            frame_size = width * height + (width / 2) * (height / 2) * 2;
            break;
        case VideoPixelFormat::NV12:
            // Y plane = width * height
            // UV interleaved = (width) * (height/2)
            frame_size = width * height + width * (height / 2);
            break;
        default:
            throw MediaError(MediaErrorCode::FormatNotSupported,
                "Unsupported video format");
    }

    // Alokasi memory untuk data
    data_.resize(frame_size);
}

VideoFrame::VideoFrame(const void* data, size_t size, VideoPixelFormat format,
                       uint32_t width, uint32_t height)
    : format_(format), width_(width), height_(height) {

    // Hitung expected size berdasarkan format dan resolusi
    size_t expected_size = 0;
    switch (format) {
        case VideoPixelFormat::RGB24:
            expected_size = width * height * 3;
            break;
        case VideoPixelFormat::RGBA32:
            expected_size = width * height * 4;
            break;
        case VideoPixelFormat::YUV420P:
            expected_size = width * height + (width / 2) * (height / 2) * 2;
            break;
        case VideoPixelFormat::NV12:
            expected_size = width * height + width * (height / 2);
            break;
        default:
            throw MediaError(MediaErrorCode::FormatNotSupported,
                "Unsupported video format");
    }

    // Verifikasi ukuran data
    if (size != expected_size) {
        throw MediaError(MediaErrorCode::InvalidParameter,
            "Invalid video data size");
    }

    // Copy data
    data_.resize(size);
    std::memcpy(data_.data(), data, size);
}

std::unique_ptr<VideoFrame> VideoFrame::clone() const {
    auto clone = std::make_unique<VideoFrame>();
    clone->data_ = data_;
    clone->format_ = format_;
    clone->width_ = width_;
    clone->height_ = height_;
    return clone;
}

// MediaStream implementation
class MediaStreamImpl : public MediaStream {
public:
    MediaStreamImpl() = default;

    // Stream management
    void start() override {
        is_active_ = true;
    }

    void stop() override {
        is_active_ = false;
    }

    bool isActive() const override {
        return is_active_;
    }

    // Track access
    std::vector<std::shared_ptr<MediaTrack>> getTracks() const override {
        return tracks_;
    }

    std::shared_ptr<MediaTrack> getTrackById(const std::string& track_id) override {
        auto it = std::find_if(tracks_.begin(), tracks_.end(),
            [&track_id](const auto& track) {
                return track->id() == track_id;
            });

        return (it != tracks_.end()) ? *it : nullptr;
    }

    // Track addition/removal
    void addTrack(std::shared_ptr<MediaTrack> track) override {
        // Check if track with same ID already exists
        auto it = std::find_if(tracks_.begin(), tracks_.end(),
            [&track](const auto& existing) {
                return existing->id() == track->id();
            });

        if (it != tracks_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Track with the same ID already exists");
        }

        tracks_.push_back(std::move(track));
    }

    void removeTrack(const std::string& track_id) override {
        auto it = std::find_if(tracks_.begin(), tracks_.end(),
            [&track_id](const auto& track) {
                return track->id() == track_id;
            });

        if (it != tracks_.end()) {
            tracks_.erase(it);
        }
    }

private:
    std::vector<std::shared_ptr<MediaTrack>> tracks_;
    bool is_active_ = false;
};

// Create empty stream
std::unique_ptr<MediaStream> MediaStream::create() {
    return std::make_unique<MediaStreamImpl>();
}

// Implementation of basic track
class MediaTrackBase : public MediaTrack {
public:
    explicit MediaTrackBase(MediaType type, std::string id)
        : type_(type), id_(std::move(id)) {}

    // Track info
    MediaType type() const override { return type_; }
    std::string id() const override { return id_; }

    // Enable/disable track
    void enable(bool enabled) override {
        is_enabled_ = enabled;
    }

    bool isEnabled() const override {
        return is_enabled_;
    }

private:
    MediaType type_;
    std::string id_;
    bool is_enabled_ = true;
};

// Audio track implementation
class AudioTrackImpl : public AudioTrack, public MediaTrackBase {
public:
    AudioTrackImpl(std::string id, const AudioCodecParams& params)
        : MediaTrackBase(MediaType::Audio, std::move(id)), params_(params) {}

    // Audio-specific methods
    AudioCodecParams params() const override {
        return params_;
    }

private:
    AudioCodecParams params_;
};

// Video track implementation
class VideoTrackImpl : public VideoTrack, public MediaTrackBase {
public:
    VideoTrackImpl(std::string id, const VideoCodecParams& params)
        : MediaTrackBase(MediaType::Video, std::move(id)), params_(params) {}

    // Video-specific methods
    VideoCodecParams params() const override {
        return params_;
    }

private:
    VideoCodecParams params_;
};

// Helper untuk membuat audio track
std::shared_ptr<AudioTrack> createAudioTrack(std::string id,
                                             const AudioCodecParams& params) {
    return std::make_shared<AudioTrackImpl>(std::move(id), params);
}

// Helper untuk membuat video track
std::shared_ptr<VideoTrack> createVideoTrack(std::string id,
                                             const VideoCodecParams& params) {
    return std::make_shared<VideoTrackImpl>(std::move(id), params);
}

} // namespace fmus::media