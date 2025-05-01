#include <fmus/media/pipeline.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/media/media.hpp>
#include <filesystem>
#include <memory>

#include <chrono>
#include <thread>
#include <random>

namespace fmus::media::pipeline {

// Forward declaration of the base node creation function
std::shared_ptr<PipelineNode> createBasePipelineNode(const std::string& name, NodeKind kind);

// Base implementation for media sources
class MediaSourceImpl : public MediaSource {
public:
    MediaSourceImpl(const std::string& name) : name_(name), is_running_(false) {}

    // PipelineNode interface
    std::string name() const override { return name_; }

    std::vector<PortDescriptor> inputPorts() const override {
        // Media sources typically don't have input ports
        return {};
    }

    std::vector<PortDescriptor> outputPorts() const override {
        // Default output ports for audio and/or video
        std::vector<PortDescriptor> ports;

        if (hasAudio()) {
            ports.push_back({
                "audio", MediaType::Audio, false, "audio/raw",
                {}
            });
        }

        if (hasVideo()) {
            ports.push_back({
                "video", MediaType::Video, false, "video/raw",
                {}
            });
        }

        return ports;
    }

    core::Task<void> connectOutput(const std::string& output_port,
                               std::shared_ptr<PipelineNode> target,
                               const std::string& input_port) override {
        // Store connection info
        if (output_port == "audio") {
            audio_target_ = target;
            audio_target_port_ = input_port;
        } else if (output_port == "video") {
            video_target_ = target;
            video_target_port_ = input_port;
        } else {
            throw MediaError(MediaErrorCode::InvalidParameter,
                           "Invalid output port: " + output_port);
        }

        co_return;
    }

    core::Task<void> disconnectOutput(const std::string& output_port) override {
        if (output_port == "audio") {
            audio_target_ = nullptr;
            audio_target_port_.clear();
        } else if (output_port == "video") {
            video_target_ = nullptr;
            video_target_port_.clear();
        } else {
            throw MediaError(MediaErrorCode::InvalidParameter,
                           "Invalid output port: " + output_port);
        }

        co_return;
    }

    core::Task<void> disconnectInput(const std::string& input_port) override {
        // Sources typically don't have input ports
        throw MediaError(MediaErrorCode::InvalidParameter,
                       "Media sources don't have input ports");
    }

    core::Task<void> initialize() override {
        // Base initialization
        co_return;
    }

    core::Task<void> start() override {
        if (is_running_) {
            co_return; // Already running
        }

        // Start processing
        is_running_ = true;
        onStarted.emit();
        co_return;
    }

    core::Task<void> stop() override {
        if (!is_running_) {
            co_return; // Already stopped
        }

        // Stop processing
        is_running_ = false;
        onStopped.emit();
        co_return;
    }

    core::Task<void> reset() override {
        // Reset source to initial state
        if (is_running_) {
            co_await stop();
        }

        co_return;
    }

    bool isRunning() const override {
        return is_running_;
    }

    void setProperty(const std::string& name, const std::any& value) override {
        properties_[name] = value;
    }

    std::any getProperty(const std::string& name) const override {
        auto it = properties_.find(name);
        if (it != properties_.end()) {
            return it->second;
        }
        return {};
    }

    bool hasProperty(const std::string& name) const override {
        return properties_.find(name) != properties_.end();
    }

protected:
    // Audio/video capabilities
    virtual bool hasAudio() const { return false; }
    virtual bool hasVideo() const { return false; }

    // Send frame to connected targets
    void sendAudioFrame(std::unique_ptr<AudioFrame> frame) {
        if (audio_target_ && is_running_) {
            // Implementation would send the frame to the target node
            // This would typically use some message passing or shared memory
        }
    }

    void sendVideoFrame(std::unique_ptr<VideoFrame> frame) {
        if (video_target_ && is_running_) {
            // Implementation would send the frame to the target node
        }
    }

    std::string name_;
    bool is_running_;
    std::unordered_map<std::string, std::any> properties_;

    // Connected targets
    std::shared_ptr<PipelineNode> audio_target_;
    std::string audio_target_port_;
    std::shared_ptr<PipelineNode> video_target_;
    std::string video_target_port_;
};

// Device source implementation
class DeviceSourceImpl : public MediaSourceImpl {
public:
    DeviceSourceImpl(const std::string& device_id)
        : MediaSourceImpl("device_source_" + device_id),
          device_id_(device_id) {
    }

    core::Task<void> initialize() override {
        co_await MediaSourceImpl::initialize();

        // Open the device
        try {
            auto devices = co_await MediaDevice::enumerateDevices();

            for (auto& device : devices) {
                if (device->id() == device_id_) {
                    device_ = std::move(device);
                    break;
                }
            }

            if (!device_) {
                throw MediaError(MediaErrorCode::DeviceNotFound,
                               "Device not found: " + device_id_);
            }

            // Open the device and get media stream
            stream_ = co_await device_->open();

            // Set up track handlers
            for (auto& track : stream_->getTracks()) {
                if (track->type() == MediaType::Audio) {
                    auto audio_track = std::dynamic_pointer_cast<AudioTrack>(track);
                    if (audio_track) {
                        audio_track->onFrame.subscribe(
                            [this](std::unique_ptr<AudioFrame> frame) {
                                sendAudioFrame(std::move(frame));
                            });
                    }
                } else if (track->type() == MediaType::Video) {
                    auto video_track = std::dynamic_pointer_cast<VideoTrack>(track);
                    if (video_track) {
                        video_track->onFrame.subscribe(
                            [this](std::unique_ptr<VideoFrame> frame) {
                                sendVideoFrame(std::move(frame));
                            });
                    }
                }
            }

        } catch (const std::exception& ex) {
            throw MediaError(MediaErrorCode::DeviceInitFailed,
                           "Failed to initialize device source: " + std::string(ex.what()));
        }
    }

    core::Task<void> start() override {
        co_await MediaSourceImpl::start();

        // Start the media stream
        if (stream_) {
            stream_->start();
        }
    }

    core::Task<void> stop() override {
        // Stop the media stream
        if (stream_) {
            stream_->stop();
        }

        co_await MediaSourceImpl::stop();
    }

    core::Task<void> reset() override {
        co_await MediaSourceImpl::reset();

        // Close and reopen the device
        stream_.reset();
        device_.reset();

        co_await initialize();
    }

protected:
    bool hasAudio() const override {
        if (!stream_) return false;

        for (auto& track : stream_->getTracks()) {
            if (track->type() == MediaType::Audio) {
                return true;
            }
        }
        return false;
    }

    bool hasVideo() const override {
        if (!stream_) return false;

        for (auto& track : stream_->getTracks()) {
            if (track->type() == MediaType::Video) {
                return true;
            }
        }
        return false;
    }

private:
    std::string device_id_;
    std::unique_ptr<MediaDevice> device_;
    std::unique_ptr<MediaStream> stream_;
};

// File source implementation
class FileSourceImpl : public MediaSourceImpl {
public:
    FileSourceImpl(const std::filesystem::path& path)
        : MediaSourceImpl("file_source_" + path.filename().string()),
          file_path_(path) {
    }

    core::Task<void> initialize() override {
        co_await MediaSourceImpl::initialize();

        // Initialize file source
        // This would typically involve opening the file, determining format,
        // and setting up decoding pipeline

        // For now, we'll just check if the file exists
        if (!std::filesystem::exists(file_path_)) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                           "File not found: " + file_path_.string());
        }

        // TODO: Initialize with FFmpeg or similar library
        // This would detect file type, codec, and prepare for decoding
    }

protected:
    // These would be determined when the file is opened
    bool hasAudio() const override { return has_audio_; }
    bool hasVideo() const override { return has_video_; }

private:
    std::filesystem::path file_path_;
    bool has_audio_ = false;
    bool has_video_ = false;
    // FFmpeg context would be stored here
};

// Network source implementation
class NetworkSourceImpl : public MediaSourceImpl {
public:
    NetworkSourceImpl(const std::string& url)
        : MediaSourceImpl("network_source_" + url),
          url_(url) {
    }

    core::Task<void> initialize() override {
        co_await MediaSourceImpl::initialize();

        // Initialize network source
        // This would typically involve opening a network connection,
        // setting up protocol handlers (RTP, RTMP, HLS, etc.)
    }

protected:
    bool hasAudio() const override { return has_audio_; }
    bool hasVideo() const override { return has_video_; }

private:
    std::string url_;
    bool has_audio_ = false;
    bool has_video_ = false;
    // Network and protocol specific data would be stored here
};

// Test source implementation
class TestSourceImpl : public MediaSourceImpl {
public:
    TestSourceImpl(MediaType type)
        : MediaSourceImpl("test_source_" + std::string(type == MediaType::Audio ? "audio" : "video")),
          type_(type) {
    }

    core::Task<void> initialize() override {
        co_await MediaSourceImpl::initialize();

        // Set up test pattern generator
    }

    core::Task<void> start() override {
        co_await MediaSourceImpl::start();

        // Start generating test frames
        if (is_running_) {
            // In a real implementation, this would start a thread or timer
            // to periodically generate frames
        }
    }

protected:
    bool hasAudio() const override { return type_ == MediaType::Audio; }
    bool hasVideo() const override { return type_ == MediaType::Video; }

private:
    MediaType type_;
};

// Factory methods
std::shared_ptr<MediaSource> MediaSource::createDeviceSource(const std::string& device_id) {
    return std::make_shared<DeviceSourceImpl>(device_id);
}

std::shared_ptr<MediaSource> MediaSource::createFileSource(const std::filesystem::path& path) {
    return std::make_shared<FileSourceImpl>(path);
}

std::shared_ptr<MediaSource> MediaSource::createNetworkSource(const std::string& url) {
    return std::make_shared<NetworkSourceImpl>(url);
}

std::shared_ptr<MediaSource> MediaSource::createTestSource(MediaType type) {
    return std::make_shared<TestSourceImpl>(type);
}

} // namespace fmus::media::pipeline