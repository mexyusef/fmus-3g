#include <gtest/gtest.h>
#include <fmus/bridge/webrtc_sip_bridge.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>

#include <memory>
#include <chrono>
#include <thread>

namespace fmus::bridge::test {

class BridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize the task scheduler for async tests
        scheduler_ = std::make_unique<core::TaskScheduler>();
        scheduler_->start(4);

        // Initialize the logger
        core::Logger::init(core::LogLevel::Debug);
    }

    void TearDown() override {
        // Stop the task scheduler
        scheduler_->stop();
        scheduler_.reset();
    }

    // Helper function to create a basic bridge configuration
    WebRtcSipBridgeConfig createTestConfig() {
        WebRtcSipBridgeConfig config;
        config.sip_uri = "sip:test@example.com";
        config.sip_password = "password";
        config.sip_proxy = "sip:proxy.example.com";
        config.room_id = "test-room";
        config.signaling_url = "wss://example.com/signaling";

        // Add ICE servers
        webrtc::WebRtcIceServer stun_server;
        stun_server.urls = "stun:stun.l.google.com:19302";
        config.webrtc_config.ice_servers.push_back(stun_server);

        return config;
    }

    std::unique_ptr<core::TaskScheduler> scheduler_;
};

// Test bridge creation
TEST_F(BridgeTest, Creation) {
    auto config = createTestConfig();
    auto bridge = WebRtcSipBridge::create(config);

    ASSERT_NE(bridge, nullptr);
    EXPECT_FALSE(bridge->isRunning());
}

// Test bridge configuration
TEST_F(BridgeTest, Configuration) {
    auto config = createTestConfig();

    // Modify some config options
    config.audio_enabled = false;
    config.video_enabled = true;
    config.transcode_audio = true;
    config.audio_codec = "OPUS";
    config.video_codec = "VP8";

    auto bridge = WebRtcSipBridge::create(config);
    ASSERT_NE(bridge, nullptr);
}

// Test bridge lifecycle (disabled to avoid actual network connections in unit tests)
TEST_F(BridgeTest, DISABLED_Lifecycle) {
    auto config = createTestConfig();
    auto bridge = WebRtcSipBridge::create(config);

    ASSERT_NE(bridge, nullptr);
    EXPECT_FALSE(bridge->isRunning());

    // Start the bridge
    bool started = false;
    bridge->start().then([&started]() {
        started = true;
    }, [](const core::Error& error) {
        FAIL() << "Failed to start bridge: " << error.what();
    });

    // Wait for bridge to start
    for (int i = 0; i < 50 && !started; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(started);
    EXPECT_TRUE(bridge->isRunning());

    // Check registration state (will depend on mock implementation)
    auto reg_state = bridge->getSipRegistrationState();
    EXPECT_NE(reg_state, sip::SipRegistrationState::None);

    // Stop the bridge
    bridge->stop();
    EXPECT_FALSE(bridge->isRunning());
}

// Test mock outbound call (disabled to avoid actual network connections)
TEST_F(BridgeTest, DISABLED_OutboundCall) {
    auto config = createTestConfig();
    auto bridge = WebRtcSipBridge::create(config);

    // Start the bridge
    bool started = false;
    bridge->start().then([&started]() {
        started = true;
    }, [](const core::Error& error) {
        FAIL() << "Failed to start bridge: " << error.what();
    });

    // Wait for bridge to start
    for (int i = 0; i < 50 && !started; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(started);

    // Make an outbound call
    bool call_initiated = false;
    bridge->makeOutboundCall("sip:test@example.com").then([&call_initiated]() {
        call_initiated = true;
    }, [](const core::Error& error) {
        FAIL() << "Failed to initiate call: " << error.what();
    });

    // Wait for call to be initiated
    for (int i = 0; i < 50 && !call_initiated; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(call_initiated);

    // Check call state
    auto call_state = bridge->getSipCallState();
    EXPECT_NE(call_state, sip::SipCallState::Terminated);

    // Stop the bridge
    bridge->stop();
}

// Test error handling (we expect these to fail as we're using mocks)
TEST_F(BridgeTest, ErrorHandling) {
    auto config = createTestConfig();
    auto bridge = WebRtcSipBridge::create(config);

    ASSERT_NE(bridge, nullptr);

    // Test making a call without starting the bridge
    try {
        bridge->makeOutboundCall("sip:test@example.com").wait();
        FAIL() << "Expected exception was not thrown";
    } catch (const std::exception& e) {
        // Expected exception
        EXPECT_STREQ(e.what(), "Bridge not running");
    }

    // Test getting clients without starting the bridge
    EXPECT_TRUE(bridge->getConnectedClients().empty());

    // Test adding a WebRTC client without starting the bridge
    try {
        bridge->addWebRtcClient("test-client").wait();
        FAIL() << "Expected exception was not thrown";
    } catch (const std::exception& e) {
        // Expected exception
        EXPECT_STREQ(e.what(), "Bridge not running");
    }
}

} // namespace fmus::bridge::test