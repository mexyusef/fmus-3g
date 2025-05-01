#include <gtest/gtest.h>
#include <fmus/media/media.hpp>
#include <fmus/media/pipeline.hpp>
#include <fmus/core/logger.hpp>

#include <memory>
#include <thread>
#include <chrono>

using namespace fmus;
using namespace fmus::media;
using namespace fmus::media::pipeline;

class MediaPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup logging
        core::Logger::init();

        // Create a pipeline with default config
        pipeline_ = std::make_unique<Pipeline>();
    }

    void TearDown() override {
        // Stop the pipeline if it's running
        if (pipeline_ && pipeline_->isRunning()) {
            auto stop_task = pipeline_->stop();
            stop_task.get();
        }

        pipeline_.reset();
    }

    std::unique_ptr<Pipeline> pipeline_;
};

// Test basic pipeline creation and destruction
TEST_F(MediaPipelineTest, CreationDestruction) {
    ASSERT_TRUE(pipeline_);
    EXPECT_FALSE(pipeline_->isRunning());
}

// Test node addition and removal
TEST_F(MediaPipelineTest, NodeManagement) {
    // Create a test media source
    auto source = MediaSource::createDeviceSource("test_device");
    ASSERT_TRUE(source);

    // Add the source to the pipeline
    pipeline_->addNode(source);

    // Verify the node was added
    auto nodes = pipeline_->getNodes();
    EXPECT_EQ(nodes.size(), 1);

    // Get the node by name
    auto retrieved_node = pipeline_->getNode(source->name());
    EXPECT_TRUE(retrieved_node);
    EXPECT_EQ(retrieved_node->name(), source->name());

    // Remove the node
    pipeline_->removeNode(source->name());

    // Verify the node was removed
    nodes = pipeline_->getNodes();
    EXPECT_EQ(nodes.size(), 0);

    // Verify retrieving non-existent node returns nullptr
    retrieved_node = pipeline_->getNode(source->name());
    EXPECT_FALSE(retrieved_node);
}

// Test pipeline initialization and startup
TEST_F(MediaPipelineTest, InitializationStartup) {
    // Create a simple pipeline with test nodes
    auto source = MediaSource::createDeviceSource("test_device");
    auto sink = MediaSink::createNullSink();

    pipeline_->addNode(source);
    pipeline_->addNode(sink);

    // Initialize the pipeline
    auto init_task = pipeline_->initialize();
    init_task.get();

    // Start the pipeline
    auto start_task = pipeline_->start();
    start_task.get();

    // Verify the pipeline is running
    EXPECT_TRUE(pipeline_->isRunning());

    // Stop the pipeline
    auto stop_task = pipeline_->stop();
    stop_task.get();

    // Verify the pipeline is stopped
    EXPECT_FALSE(pipeline_->isRunning());
}

// Test node connectivity
TEST_F(MediaPipelineTest, NodeConnectivity) {
    // Create nodes for a simple video processing pipeline
    auto source = MediaSource::createDeviceSource("test_device");
    auto filter = MediaFilter::createResizeFilter(640, 480);
    auto sink = MediaSink::createNullSink();

    // Add nodes to the pipeline
    pipeline_->addNode(source);
    pipeline_->addNode(filter);
    pipeline_->addNode(sink);

    // Connect the nodes
    auto connect_task = pipeline_->connectNodes(
        source->name(), "video_out",
        filter->name(), "video_in");
    connect_task.get();

    connect_task = pipeline_->connectNodes(
        filter->name(), "video_out",
        sink->name(), "video_in");
    connect_task.get();

    // Now test the pipeline operation
    auto init_task = pipeline_->initialize();
    init_task.get();

    auto start_task = pipeline_->start();
    start_task.get();

    // Run the pipeline for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop the pipeline
    auto stop_task = pipeline_->stop();
    stop_task.get();

    EXPECT_FALSE(pipeline_->isRunning());
}

// Test error handling
TEST_F(MediaPipelineTest, ErrorHandling) {
    // Set up an error handler
    bool error_received = false;
    std::string error_message;

    pipeline_->onError.connect([&error_received, &error_message](const core::Error& error) {
        error_received = true;
        error_message = error.what();
    });

    // Try to connect non-existent nodes, which should trigger an error
    try {
        auto connect_task = pipeline_->connectNodes(
            "non_existent_source", "video_out",
            "non_existent_sink", "video_in");
        connect_task.get();
        FAIL() << "Expected an exception for connecting non-existent nodes";
    } catch (const MediaError& e) {
        EXPECT_EQ(e.mediaCode(), MediaErrorCode::InvalidParameter);
        EXPECT_TRUE(error_received);
        EXPECT_FALSE(error_message.empty());
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}