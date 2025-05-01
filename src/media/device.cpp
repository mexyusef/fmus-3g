#include <fmus/media/media.hpp>
#include <fmus/core/logger.hpp>

#include <unordered_map>
#include <random>
#include <sstream>

namespace fmus::media {

// Forward declare helper functions
std::shared_ptr<AudioTrack> createAudioTrack(std::string id,
                                             const AudioCodecParams& params);
std::shared_ptr<VideoTrack> createVideoTrack(std::string id,
                                             const VideoCodecParams& params);

// Base class for media devices
class MediaDeviceImpl : public MediaDevice {
public:
    MediaDeviceImpl(std::string id, std::string name, MediaDeviceType type)
        : id_(std::move(id)), name_(std::move(name)), type_(type) {}

    // Device info
    std::string id() const override { return id_; }
    std::string name() const override { return name_; }
    MediaDeviceType type() const override { return type_; }

protected:
    std::string id_;
    std::string name_;
    MediaDeviceType type_;
};

// Audio input device implementation
class AudioInputDeviceImpl : public MediaDeviceImpl {
public:
    AudioInputDeviceImpl(std::string id, std::string name)
        : MediaDeviceImpl(std::move(id), std::move(name), MediaDeviceType::AudioInput) {
        // Setup default capabilities
        capabilities_.sample_rates = {8000, 16000, 44100, 48000};
        capabilities_.channels = {1, 2};
    }

    // Device capabilities
    MediaDeviceCapabilities capabilities() const override {
        return capabilities_;
    }

    // Open device
    core::Task<std::unique_ptr<MediaStream>> open() override {
        auto stream = MediaStream::create();

        // Setup audio track for this device
        AudioCodecParams params;
        params.codec_name = "pcm";
        params.sample_rate = 44100;
        params.channels = 2;
        params.sample_format = AudioSampleFormat::S16;

        // Tambahkan track ke stream
        auto track = createAudioTrack(generateTrackId(), params);
        stream->addTrack(track);

        // Di implementasi sebenarnya, kita akan menginisialisasi hardware device
        // Untuk sekarang, kita hanya simulasikan
        // TODO: Implement actual device initialization

        co_return std::move(stream);
    }

private:
    MediaDeviceCapabilities capabilities_;

    // Generate random track ID
    std::string generateTrackId() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << "audio-" << std::hex;
        for (int i = 0; i < 8; ++i) {
            ss << dis(gen);
        }
        return ss.str();
    }
};

// Audio output device implementation
class AudioOutputDeviceImpl : public MediaDeviceImpl {
public:
    AudioOutputDeviceImpl(std::string id, std::string name)
        : MediaDeviceImpl(std::move(id), std::move(name), MediaDeviceType::AudioOutput) {
        // Setup default capabilities
        capabilities_.sample_rates = {8000, 16000, 44100, 48000};
        capabilities_.channels = {1, 2};
    }

    // Device capabilities
    MediaDeviceCapabilities capabilities() const override {
        return capabilities_;
    }

    // Open device
    core::Task<std::unique_ptr<MediaStream>> open() override {
        auto stream = MediaStream::create();

        // Setup audio track for this device
        AudioCodecParams params;
        params.codec_name = "pcm";
        params.sample_rate = 44100;
        params.channels = 2;
        params.sample_format = AudioSampleFormat::S16;

        // Tambahkan track ke stream
        auto track = createAudioTrack(generateTrackId(), params);
        stream->addTrack(track);

        // Di implementasi sebenarnya, kita akan menginisialisasi hardware device
        // Untuk sekarang, kita hanya simulasikan
        // TODO: Implement actual device initialization

        co_return std::move(stream);
    }

private:
    MediaDeviceCapabilities capabilities_;

    // Generate random track ID
    std::string generateTrackId() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << "audio-" << std::hex;
        for (int i = 0; i < 8; ++i) {
            ss << dis(gen);
        }
        return ss.str();
    }
};

// Video input device implementation
class VideoInputDeviceImpl : public MediaDeviceImpl {
public:
    VideoInputDeviceImpl(std::string id, std::string name)
        : MediaDeviceImpl(std::move(id), std::move(name), MediaDeviceType::VideoInput) {
        // Setup default capabilities
        capabilities_.resolutions = {
            {640, 480}, {800, 600}, {1280, 720}, {1920, 1080}
        };
        capabilities_.frame_rates = {15.0f, 24.0f, 30.0f, 60.0f};
    }

    // Device capabilities
    MediaDeviceCapabilities capabilities() const override {
        return capabilities_;
    }

    // Open device
    core::Task<std::unique_ptr<MediaStream>> open() override {
        auto stream = MediaStream::create();

        // Setup video track for this device
        VideoCodecParams params;
        params.codec_name = "raw";
        params.width = 1280;
        params.height = 720;
        params.frame_rate = 30.0f;
        params.pixel_format = VideoPixelFormat::YUV420P;

        // Tambahkan track ke stream
        auto track = createVideoTrack(generateTrackId(), params);
        stream->addTrack(track);

        // Di implementasi sebenarnya, kita akan menginisialisasi hardware device
        // Untuk sekarang, kita hanya simulasikan
        // TODO: Implement actual device initialization

        co_return std::move(stream);
    }

private:
    MediaDeviceCapabilities capabilities_;

    // Generate random track ID
    std::string generateTrackId() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << "video-" << std::hex;
        for (int i = 0; i < 8; ++i) {
            ss << dis(gen);
        }
        return ss.str();
    }
};

// Factory method to enumerate devices
core::Task<std::vector<std::unique_ptr<MediaDevice>>> MediaDevice::enumerateDevices() {
    std::vector<std::unique_ptr<MediaDevice>> devices;

    // Di implementasi sebenarnya, kita akan melakukan penemuan hardware yang sebenarnya
    // Untuk sekarang, kita simulasikan beberapa device
    // TODO: Implement actual device discovery

    // Simulasikan audio input device (microphone)
    devices.push_back(std::make_unique<AudioInputDeviceImpl>(
        "audio-in-1", "Default Microphone"));

    // Simulasikan audio output device (speaker)
    devices.push_back(std::make_unique<AudioOutputDeviceImpl>(
        "audio-out-1", "Default Speaker"));

    // Simulasikan video input device (camera)
    devices.push_back(std::make_unique<VideoInputDeviceImpl>(
        "video-in-1", "Default Camera"));

    co_return std::move(devices);
}

// Media source implementation
class MediaSourceImpl : public MediaSource {
public:
    MediaSourceImpl(std::shared_ptr<MediaStream> stream)
        : stream_(std::move(stream)) {}

    core::Task<void> open() override {
        if (!stream_) {
            throw MediaError(MediaErrorCode::StreamNotFound, "No stream available");
        }

        stream_->start();
        co_return;
    }

    void close() override {
        if (stream_) {
            stream_->stop();
        }
    }

    std::shared_ptr<MediaStream> stream() override {
        return stream_;
    }

protected:
    std::shared_ptr<MediaStream> stream_;
};

// Media sink implementation
class MediaSinkImpl : public MediaSink {
public:
    MediaSinkImpl() = default;

    core::Task<void> open() override {
        if (!stream_) {
            throw MediaError(MediaErrorCode::StreamNotFound, "No stream set");
        }

        stream_->start();
        co_return;
    }

    void close() override {
        if (stream_) {
            stream_->stop();
        }
    }

    void setStream(std::shared_ptr<MediaStream> stream) override {
        stream_ = std::move(stream);
    }

private:
    std::shared_ptr<MediaStream> stream_;
};

// Device source implementation
class DeviceSourceImpl : public MediaSourceImpl {
public:
    DeviceSourceImpl(std::unique_ptr<MediaDevice> device)
        : MediaSourceImpl(nullptr), device_(std::move(device)) {}

    core::Task<void> open() override {
        if (!device_) {
            throw MediaError(MediaErrorCode::DeviceNotFound, "Device not available");
        }

        auto stream_ptr = co_await device_->open();
        stream_ = std::shared_ptr<MediaStream>(stream_ptr.release());

        co_await MediaSourceImpl::open();
    }

private:
    std::unique_ptr<MediaDevice> device_;

    void setStream(std::shared_ptr<MediaStream> stream) {
        stream_ = stream;
    }
};

// Factory methods for sources and sinks
std::unique_ptr<MediaSource> MediaSource::createDeviceSource(
    std::unique_ptr<MediaDevice> device) {
    return std::make_unique<DeviceSourceImpl>(std::move(device));
}

std::unique_ptr<MediaSource> MediaSource::createFileSource(
    const std::filesystem::path& file_path) {
    // TODO: Implement file source
    throw MediaError(MediaErrorCode::NotImplemented, "File source not implemented yet");
}

std::unique_ptr<MediaSource> MediaSource::createNetworkSource(
    const network::NetworkAddress& address) {
    // TODO: Implement network source
    throw MediaError(MediaErrorCode::NotImplemented, "Network source not implemented yet");
}

std::unique_ptr<MediaSink> MediaSink::createDeviceSink(
    std::unique_ptr<MediaDevice> device) {
    // TODO: Implement device sink properly
    return std::make_unique<MediaSinkImpl>();
}

std::unique_ptr<MediaSink> MediaSink::createFileSink(
    const std::filesystem::path& file_path) {
    // TODO: Implement file sink
    throw MediaError(MediaErrorCode::NotImplemented, "File sink not implemented yet");
}

std::unique_ptr<MediaSink> MediaSink::createNetworkSink(
    const network::NetworkAddress& address) {
    // TODO: Implement network sink
    throw MediaError(MediaErrorCode::NotImplemented, "Network sink not implemented yet");
}

} // namespace fmus::media