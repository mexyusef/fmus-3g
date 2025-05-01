#pragma once

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <filesystem>

#include <fmus/core/error.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/event.hpp>
#include <fmus/network/network.hpp>

namespace fmus::media {

// Forward declarations
class AudioFrame;
class VideoFrame;
class MediaCodec;
class MediaDevice;
class MediaStream;
class MediaTrack;
class AudioTrack;
class VideoTrack;
class MediaSource;
class MediaSink;

// Media error codes
enum class MediaErrorCode {
    Success = 0,
    DeviceNotFound,
    DeviceInUse,
    DeviceInitFailed,
    CodecNotSupported,
    EncodingFailed,
    DecodingFailed,
    FormatNotSupported,
    StreamNotFound,
    InvalidParameter,
    PermissionDenied,
    EndOfStream,
    ResolutionNotSupported,
    NotImplemented,
    UnknownError
};

// Media error exception
class MediaError : public core::Error {
public:
    explicit MediaError(MediaErrorCode code, const std::string& message);
    MediaErrorCode mediaCode() const noexcept { return code_; }

private:
    MediaErrorCode code_;
};

// Media types
enum class MediaType {
    Audio,
    Video,
    Unknown
};

// Audio sample format
enum class AudioSampleFormat {
    S16,    // Signed 16-bit PCM
    S32,    // Signed 32-bit PCM
    F32,    // 32-bit float
    Unknown
};

// Video pixel format
enum class VideoPixelFormat {
    RGB24,      // 24-bit RGB
    RGBA32,     // 32-bit RGBA
    YUV420P,    // YUV 4:2:0 planar
    NV12,       // YUV 4:2:0 semi-planar
    Unknown
};

// Audio frame containing PCM samples
class AudioFrame {
public:
    // Constructors
    AudioFrame() = default;
    AudioFrame(AudioSampleFormat format, uint32_t sample_rate, uint8_t channels,
               size_t num_samples);
    AudioFrame(const void* data, size_t size, AudioSampleFormat format,
               uint32_t sample_rate, uint8_t channels);

    // Getters
    const uint8_t* data() const noexcept { return data_.data(); }
    uint8_t* data() noexcept { return data_.data(); }
    size_t size() const noexcept { return data_.size(); }
    AudioSampleFormat format() const noexcept { return format_; }
    uint32_t sampleRate() const noexcept { return sample_rate_; }
    uint8_t channels() const noexcept { return channels_; }
    size_t numSamples() const noexcept { return num_samples_; }

    // Duration of the frame
    std::chrono::microseconds duration() const;

    // Clone the frame
    std::unique_ptr<AudioFrame> clone() const;

    // Move operations
    AudioFrame(AudioFrame&&) noexcept = default;
    AudioFrame& operator=(AudioFrame&&) noexcept = default;

    // Delete copy operations
    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;

private:
    std::vector<uint8_t> data_;
    AudioSampleFormat format_ = AudioSampleFormat::Unknown;
    uint32_t sample_rate_ = 0;
    uint8_t channels_ = 0;
    size_t num_samples_ = 0;
};

// Video frame containing pixel data
class VideoFrame {
public:
    // Constructors
    VideoFrame() = default;
    VideoFrame(VideoPixelFormat format, uint32_t width, uint32_t height);
    VideoFrame(const void* data, size_t size, VideoPixelFormat format,
               uint32_t width, uint32_t height);

    // Getters
    const uint8_t* data() const noexcept { return data_.data(); }
    uint8_t* data() noexcept { return data_.data(); }
    size_t size() const noexcept { return data_.size(); }
    VideoPixelFormat format() const noexcept { return format_; }
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }

    // Clone the frame
    std::unique_ptr<VideoFrame> clone() const;

    // Move operations
    VideoFrame(VideoFrame&&) noexcept = default;
    VideoFrame& operator=(VideoFrame&&) noexcept = default;

    // Delete copy operations
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;

private:
    std::vector<uint8_t> data_;
    VideoPixelFormat format_ = VideoPixelFormat::Unknown;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

// Media codec parameters
struct AudioCodecParams {
    std::string codec_name;
    uint32_t bit_rate = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    AudioSampleFormat sample_format = AudioSampleFormat::Unknown;
};

struct VideoCodecParams {
    std::string codec_name;
    uint32_t bit_rate = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float frame_rate = 0.0f;
    VideoPixelFormat pixel_format = VideoPixelFormat::Unknown;
};

// Media codec interface
class MediaCodec {
public:
    virtual ~MediaCodec() = default;

    // Codec info
    virtual MediaType type() const = 0;
    virtual std::string name() const = 0;

    // Create codec instances
    static std::unique_ptr<MediaCodec> createAudioEncoder(const AudioCodecParams& params);
    static std::unique_ptr<MediaCodec> createAudioDecoder(const AudioCodecParams& params);
    static std::unique_ptr<MediaCodec> createVideoEncoder(const VideoCodecParams& params);
    static std::unique_ptr<MediaCodec> createVideoDecoder(const VideoCodecParams& params);
};

// Audio codec interface
class AudioCodec : public MediaCodec {
public:
    // Implementation of MediaCodec interface
    MediaType type() const override { return MediaType::Audio; }

    // Encode/decode methods
    virtual core::Task<std::unique_ptr<AudioFrame>> encode(
        const AudioFrame& frame) = 0;
    virtual core::Task<std::unique_ptr<AudioFrame>> decode(
        const AudioFrame& frame) = 0;

    // Get codec parameters
    virtual AudioCodecParams params() const = 0;
};

// Video codec interface
class VideoCodec : public MediaCodec {
public:
    // Implementation of MediaCodec interface
    MediaType type() const override { return MediaType::Video; }

    // Encode/decode methods
    virtual core::Task<std::unique_ptr<VideoFrame>> encode(
        const VideoFrame& frame) = 0;
    virtual core::Task<std::unique_ptr<VideoFrame>> decode(
        const VideoFrame& frame) = 0;

    // Get codec parameters
    virtual VideoCodecParams params() const = 0;
};

// Media device types
enum class MediaDeviceType {
    AudioInput,
    AudioOutput,
    VideoInput,
    VideoOutput,
    Unknown
};

// Media device capabilities
struct MediaDeviceCapabilities {
    std::vector<uint32_t> sample_rates;    // Audio: supported sample rates
    std::vector<uint8_t> channels;         // Audio: supported channel counts
    std::vector<std::pair<uint32_t, uint32_t>> resolutions;  // Video: supported resolutions
    std::vector<float> frame_rates;        // Video: supported frame rates
};

// Media device interface
class MediaDevice {
public:
    virtual ~MediaDevice() = default;

    // Device info
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual MediaDeviceType type() const = 0;

    // Device capabilities
    virtual MediaDeviceCapabilities capabilities() const = 0;

    // Enumerate devices
    static core::Task<std::vector<std::unique_ptr<MediaDevice>>> enumerateDevices();

    // Open device
    virtual core::Task<std::unique_ptr<MediaStream>> open() = 0;
};

// Media track base class
class MediaTrack {
public:
    virtual ~MediaTrack() = default;

    // Track info
    virtual MediaType type() const = 0;
    virtual std::string id() const = 0;

    // Enable/disable track
    virtual void enable(bool enabled) = 0;
    virtual bool isEnabled() const = 0;
};

// Audio track interface
class AudioTrack : public MediaTrack {
public:
    // Implementation of MediaTrack interface
    MediaType type() const override { return MediaType::Audio; }

    // Audio-specific methods
    virtual AudioCodecParams params() const = 0;

    // Events
    core::EventEmitter<std::unique_ptr<AudioFrame>> onFrame;
};

// Video track interface
class VideoTrack : public MediaTrack {
public:
    // Implementation of MediaTrack interface
    MediaType type() const override { return MediaType::Video; }

    // Video-specific methods
    virtual VideoCodecParams params() const = 0;

    // Events
    core::EventEmitter<std::unique_ptr<VideoFrame>> onFrame;
};

// Media stream interface
class MediaStream {
public:
    virtual ~MediaStream() = default;

    // Stream management
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;

    // Track access
    virtual std::vector<std::shared_ptr<MediaTrack>> getTracks() const = 0;
    virtual std::shared_ptr<MediaTrack> getTrackById(const std::string& track_id) = 0;

    // Track addition/removal
    virtual void addTrack(std::shared_ptr<MediaTrack> track) = 0;
    virtual void removeTrack(const std::string& track_id) = 0;

    // Create empty stream
    static std::unique_ptr<MediaStream> create();
};

// Media source interface
class MediaSource {
public:
    virtual ~MediaSource() = default;

    // Open/close the source
    virtual core::Task<void> open() = 0;
    virtual void close() = 0;

    // Get media stream
    virtual std::shared_ptr<MediaStream> stream() = 0;

    // Factory methods
    static std::unique_ptr<MediaSource> createDeviceSource(
        std::unique_ptr<MediaDevice> device);
    static std::unique_ptr<MediaSource> createFileSource(
        const std::filesystem::path& file_path);
    static std::unique_ptr<MediaSource> createNetworkSource(
        const network::NetworkAddress& address);
};

// Media sink interface
class MediaSink {
public:
    virtual ~MediaSink() = default;

    // Open/close the sink
    virtual core::Task<void> open() = 0;
    virtual void close() = 0;

    // Set media stream
    virtual void setStream(std::shared_ptr<MediaStream> stream) = 0;

    // Factory methods
    static std::unique_ptr<MediaSink> createDeviceSink(
        std::unique_ptr<MediaDevice> device);
    static std::unique_ptr<MediaSink> createFileSink(
        const std::filesystem::path& file_path);
    static std::unique_ptr<MediaSink> createNetworkSink(
        const network::NetworkAddress& address);
};

} // namespace fmus::media