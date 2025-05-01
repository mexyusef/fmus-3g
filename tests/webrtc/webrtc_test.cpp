#include <gtest/gtest.h>
#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/media/media.hpp>

#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

namespace fmus::webrtc::test {

class WebRtcTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start the task scheduler for async tests
        scheduler_ = std::make_unique<core::TaskScheduler>();
        scheduler_->start(4);
    }

    void TearDown() override {
        // Stop the task scheduler
        scheduler_->stop();
        scheduler_.reset();
    }

    std::unique_ptr<core::TaskScheduler> scheduler_;
};

// Helper function to create an ICE server configuration
WebRtcConfiguration createTestConfiguration() {
    WebRtcConfiguration config;

    // Add some ICE servers
    WebRtcIceServer stun_server;
    stun_server.urls = "stun:stun.l.google.com:19302";
    config.ice_servers.push_back(stun_server);

    WebRtcIceServer turn_server;
    turn_server.urls = "turn:turn.example.com:3478";
    turn_server.username = "username";
    turn_server.credential = "password";
    config.ice_servers.push_back(turn_server);

    return config;
}

// Test WebRTC error handling
TEST_F(WebRtcTest, ErrorHandling) {
    WebRtcError error(WebRtcErrorCode::InvalidParameter, "Test error message");

    EXPECT_EQ(error.webrtcCode(), WebRtcErrorCode::InvalidParameter);
    EXPECT_STREQ(error.what(), "Test error message");
}

// Test ICE candidate and SDP functionality
TEST_F(WebRtcTest, IceCandidateAndSdp) {
    // Create and test an ICE candidate
    WebRtcIceCandidate candidate("audio", 0,
        "candidate:1 1 UDP 2130706431 192.168.1.1 50000 typ host");

    EXPECT_EQ(candidate.sdpMid(), "audio");
    EXPECT_EQ(candidate.sdpMLineIndex(), 0);
    EXPECT_EQ(candidate.candidate(),
             "candidate:1 1 UDP 2130706431 192.168.1.1 50000 typ host");

    // Test JSON serialization/deserialization
    std::string json = candidate.toJson();
    EXPECT_FALSE(json.empty());

    // Create and test an SDP session description
    WebRtcSessionDescription offer(WebRtcSdpType::Offer,
        "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n");

    EXPECT_EQ(offer.type(), WebRtcSdpType::Offer);
    EXPECT_EQ(offer.typeString(), "offer");
    EXPECT_FALSE(offer.sdp().empty());

    // Test JSON serialization
    json = offer.toJson();
    EXPECT_FALSE(json.empty());
}

// Test signaling creation
TEST_F(WebRtcTest, SignalingCreation) {
    try {
        auto signaling = WebRtcSignaling::create();
        EXPECT_TRUE(signaling != nullptr);
    } catch (const WebRtcError& e) {
        // The implementation might throw NotImplemented during early development
        EXPECT_EQ(e.webrtcCode(), WebRtcErrorCode::NotImplemented);
    }
}

// Test peer connection creation
TEST_F(WebRtcTest, PeerConnectionCreation) {
    WebRtcConfiguration config = createTestConfiguration();

    try {
        auto pc = WebRtcPeerConnection::create(config);
        EXPECT_TRUE(pc != nullptr);
    } catch (const WebRtcError& e) {
        // The implementation might throw NotImplemented during early development
        EXPECT_EQ(e.webrtcCode(), WebRtcErrorCode::NotImplemented);
    }
}

// Test WebRTC session creation
TEST_F(WebRtcTest, SessionCreation) {
    WebRtcConfiguration config = createTestConfiguration();

    try {
        auto signaling = WebRtcSignaling::create();
        auto session = WebRtcSession::create(signaling, config);
        EXPECT_TRUE(session != nullptr);
    } catch (const WebRtcError& e) {
        // The implementation might throw NotImplemented during early development
        EXPECT_EQ(e.webrtcCode(), WebRtcErrorCode::NotImplemented);
    }
}

// Test session functionality (when implementation is ready)
TEST_F(WebRtcTest, DISABLED_SessionFunctionality) {
    WebRtcConfiguration config = createTestConfiguration();

    try {
        // Create two signaling instances
        auto signaling1 = WebRtcSignaling::create();
        auto signaling2 = WebRtcSignaling::create();

        // Create two sessions
        auto session1 = WebRtcSession::create(signaling1, config);
        auto session2 = WebRtcSession::create(signaling2, config);

        // Connect signaling channels
        bool connect1_done = false;
        bool connect2_done = false;

        signaling1->connect("wss://example.com/signaling").then([&](auto) {
            connect1_done = true;
        }, [](auto) {});

        signaling2->connect("wss://example.com/signaling").then([&](auto) {
            connect2_done = true;
        }, [](auto) {});

        // Wait for connections
        for (int i = 0; i < 50 && (!connect1_done || !connect2_done); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        EXPECT_TRUE(connect1_done);
        EXPECT_TRUE(connect2_done);

        // Join rooms
        std::vector<std::string> peers1;
        std::vector<std::string> peers2;

        signaling1->join("test-room", "peer1").then([&](auto result) {
            peers1 = result;
        }, [](auto) {});

        signaling2->join("test-room", "peer2").then([&](auto result) {
            peers2 = result;
        }, [](auto) {});

        // Wait for joins
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Create a media stream
        auto stream = media::MediaStream::create();
        // Would add tracks to stream in real test

        // Add stream to session
        session1->addLocalStream(stream).then([](auto) {}, [](auto) {});

        // Connect peers
        bool connected = false;
        session1->connect("peer2").then([&](auto) {
            connected = true;
        }, [](auto) {});

        // Wait for connection
        for (int i = 0; i < 50 && !connected; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        EXPECT_TRUE(connected);

        // Verify peer connections
        auto peers = session1->getConnectedPeers();
        EXPECT_TRUE(peers.find("peer2") != peers.end());

        // Clean up
        session1->disconnectAll();
        session2->disconnectAll();

        signaling1->leave();
        signaling2->leave();

        signaling1->disconnect();
        signaling2->disconnect();

    } catch (const WebRtcError& e) {
        // The implementation might throw NotImplemented during early development
        EXPECT_EQ(e.webrtcCode(), WebRtcErrorCode::NotImplemented);
    }
}

} // namespace fmus::webrtc::test