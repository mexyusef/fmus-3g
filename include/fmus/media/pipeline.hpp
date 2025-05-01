#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <any>
#include <functional>
#include <chrono>
#include <optional>
#include <atomic>
#include <mutex>

#include <fmus/core/error.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/event.hpp>
#include <fmus/media/media.hpp>

namespace fmus::media::pipeline {

// Forward declarations
class Pipeline;
class PipelineNode;
class MediaSource;
class MediaSink;
class MediaFilter;
class MediaProcessor;

// Pipeline configuration
struct PipelineConfig {
    bool auto_connect = true;  // Automatically connect compatible nodes
    bool hardware_acceleration = true;  // Use hardware acceleration when available
    std::unordered_map<std::string, std::any> properties;  // Additional properties
};

// Node kinds
enum class NodeKind {
    Source,     // Generates media (e.g., camera, file, network)
    Sink,       // Consumes media (e.g., display, file, network)
    Filter,     // Transforms media (e.g., resize, crop, format conversion)
    Processor,  // Processes media (e.g., encoder, decoder, mixer)
    Custom      // Custom node type
};

// Node port description
struct PortDescriptor {
    std::string name;
    MediaType type;
    bool is_input;
    std::string format;  // MIME type or format description
    std::unordered_map<std::string, std::any> properties;
};

// Base class for pipeline nodes
class PipelineNode : public std::enable_shared_from_this<PipelineNode> {
public:
    virtual ~PipelineNode() = default;

    // Node identification
    virtual std::string name() const = 0;
    virtual NodeKind kind() const = 0;

    // Port management
    virtual std::vector<PortDescriptor> inputPorts() const = 0;
    virtual std::vector<PortDescriptor> outputPorts() const = 0;

    // Connect nodes
    virtual core::Task<void> connectOutput(const std::string& output_port,
                                           std::shared_ptr<PipelineNode> target,
                                           const std::string& input_port) = 0;

    virtual core::Task<void> disconnectOutput(const std::string& output_port) = 0;
    virtual core::Task<void> disconnectInput(const std::string& input_port) = 0;

    // Node lifecycle
    virtual core::Task<void> initialize() = 0;
    virtual core::Task<void> start() = 0;
    virtual core::Task<void> stop() = 0;
    virtual core::Task<void> reset() = 0;
    virtual bool isRunning() const = 0;

    // Events
    core::EventEmitter<> onStarted;
    core::EventEmitter<> onStopped;
    core::EventEmitter<const core::Error&> onError;

    // Property access
    virtual void setProperty(const std::string& name, const std::any& value) = 0;
    virtual std::any getProperty(const std::string& name) const = 0;
    virtual bool hasProperty(const std::string& name) const = 0;
};

// Source node interface
class MediaSource : public PipelineNode {
public:
    NodeKind kind() const override { return NodeKind::Source; }

    // Create source nodes
    static std::shared_ptr<MediaSource> createDeviceSource(const std::string& device_id);
    static std::shared_ptr<MediaSource> createFileSource(const std::filesystem::path& path);
    static std::shared_ptr<MediaSource> createNetworkSource(const std::string& url);
    static std::shared_ptr<MediaSource> createTestSource(MediaType type);
};

// Sink node interface
class MediaSink : public PipelineNode {
public:
    NodeKind kind() const override { return NodeKind::Sink; }

    // Create sink nodes
    static std::shared_ptr<MediaSink> createDeviceSink(const std::string& device_id);
    static std::shared_ptr<MediaSink> createFileSink(const std::filesystem::path& path);
    static std::shared_ptr<MediaSink> createNetworkSink(const std::string& url);
    static std::shared_ptr<MediaSink> createNullSink();
};

// Filter node interface
class MediaFilter : public PipelineNode {
public:
    NodeKind kind() const override { return NodeKind::Filter; }

    // Create filter nodes
    static std::shared_ptr<MediaFilter> createResizeFilter(uint32_t width, uint32_t height);
    static std::shared_ptr<MediaFilter> createFormatConverterFilter(VideoPixelFormat format);
    static std::shared_ptr<MediaFilter> createFormatConverterFilter(AudioSampleFormat format,
                                                                   uint32_t sample_rate = 0,
                                                                   uint8_t channels = 0);
    static std::shared_ptr<MediaFilter> createCustomFilter(
        std::function<core::Task<std::unique_ptr<AudioFrame>>(std::unique_ptr<AudioFrame>)> audio_processor,
        std::function<core::Task<std::unique_ptr<VideoFrame>>(std::unique_ptr<VideoFrame>)> video_processor);
};

// Processor node interface
class MediaProcessor : public PipelineNode {
public:
    NodeKind kind() const override { return NodeKind::Processor; }

    // Create processor nodes
    static std::shared_ptr<MediaProcessor> createEncoderProcessor(const std::string& codec_name);
    static std::shared_ptr<MediaProcessor> createDecoderProcessor(const std::string& codec_name);
    static std::shared_ptr<MediaProcessor> createMixerProcessor();
    static std::shared_ptr<MediaProcessor> createSplitterProcessor();
};

// A complete media pipeline
class Pipeline {
public:
    Pipeline(const PipelineConfig& config = {});
    ~Pipeline();

    // Node management
    void addNode(std::shared_ptr<PipelineNode> node);
    void removeNode(const std::string& node_name);
    std::shared_ptr<PipelineNode> getNode(const std::string& node_name) const;
    std::vector<std::shared_ptr<PipelineNode>> getNodes() const;

    // Node connection
    core::Task<void> connectNodes(const std::string& source_node, const std::string& output_port,
                                  const std::string& target_node, const std::string& input_port);

    // Pipeline lifecycle
    core::Task<void> initialize();
    core::Task<void> start();
    core::Task<void> stop();
    core::Task<void> reset();
    bool isRunning() const;

    // Events
    core::EventEmitter<> onStarted;
    core::EventEmitter<> onStopped;
    core::EventEmitter<const core::Error&> onError;

private:
    PipelineConfig config_;
    std::unordered_map<std::string, std::shared_ptr<PipelineNode>> nodes_;
    std::atomic<bool> is_running_{false};
    mutable std::mutex mutex_;
};

} // namespace fmus::media::pipeline