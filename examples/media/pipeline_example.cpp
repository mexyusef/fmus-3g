#include <fmus/media/media.hpp>
#include <fmus/media/pipeline.hpp>
#include <fmus/core/logger.hpp>

#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace fmus;
using namespace fmus::media;
using namespace fmus::media::pipeline;

// Simple example of using the media pipeline
int main(int argc, char* argv[]) {
    try {
        // Setup logging
        core::Logger::init();
        auto logger = core::Logger::get("PipelineExample");
        logger->info("Starting pipeline example application");

        // Create a pipeline
        PipelineConfig config;
        config.auto_connect = true;
        config.hardware_acceleration = true;

        auto pipeline = std::make_unique<Pipeline>(config);

        // Find available video devices
        logger->info("Enumerating media devices...");
        auto devices_task = MediaDevice::enumerateDevices();
        auto devices = devices_task.get();

        // Display available devices
        logger->info("Found {} media devices:", devices.size());
        for (const auto& device : devices) {
            logger->info("  - {}: {}", device->id(), device->name());
        }

        // Find the first video device
        std::string video_device_id;
        for (const auto& device : devices) {
            if (device->type() == MediaDeviceType::VideoInput) {
                video_device_id = device->id();
                logger->info("Selected video device: {} ({})", device->id(), device->name());
                break;
            }
        }

        if (video_device_id.empty()) {
            logger->error("No video input device found");
            return 1;
        }

        // Create source node from the device
        auto source = MediaSource::createDeviceSource(video_device_id);
        pipeline->addNode(source);

        // Create a resize filter to scale the video down
        auto resize_filter = MediaFilter::createResizeFilter(640, 480);
        pipeline->addNode(resize_filter);

        // Create a format converter to convert to RGB24
        auto format_converter = MediaFilter::createFormatConverterFilter(VideoPixelFormat::RGB24);
        pipeline->addNode(format_converter);

        // Create a null sink to consume the video frames
        auto sink = MediaSink::createNullSink();
        pipeline->addNode(sink);

        // Connect the nodes
        auto connect_task = pipeline->connectNodes(
            source->name(), "video_out",
            resize_filter->name(), "video_in");
        connect_task.get();

        connect_task = pipeline->connectNodes(
            resize_filter->name(), "video_out",
            format_converter->name(), "video_in");
        connect_task.get();

        connect_task = pipeline->connectNodes(
            format_converter->name(), "video_out",
            sink->name(), "video_in");
        connect_task.get();

        // Setup error handler
        pipeline->onError.connect([&logger](const core::Error& error) {
            logger->error("Pipeline error: {}", error.what());
        });

        // Initialize the pipeline
        logger->info("Initializing pipeline...");
        auto init_task = pipeline->initialize();
        init_task.get();

        // Start the pipeline
        logger->info("Starting pipeline...");
        auto start_task = pipeline->start();
        start_task.get();

        // Run for a while
        logger->info("Pipeline running, press Enter to stop...");
        std::cin.get();

        // Stop the pipeline
        logger->info("Stopping pipeline...");
        auto stop_task = pipeline->stop();
        stop_task.get();

        logger->info("Pipeline example completed successfully");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}