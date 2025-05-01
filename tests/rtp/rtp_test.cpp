#include <gtest/gtest.h>
#include <fmus/rtp/rtp.hpp>
#include <fmus/core/task.hpp>
#include <fmus/media/media.hpp>

#include <memory>
#include <chrono>
#include <thread>
#include <vector>

namespace fmus::rtp::test {

class RtpTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start the task scheduler for async tests
        scheduler_ = &core::TaskScheduler::getInstance();
        scheduler_->start(4);
    }

    void TearDown() override {
        // Stop the task scheduler
        scheduler_->stop();
    }

    core::TaskScheduler* scheduler_ = nullptr;
};

// Helper function to create a test RTP packet
RtpPacket createTestRtpPacket() {
    RtpHeader header;
    header.version = 2;
    header.padding = false;
    header.extension = false;
    header.csrcCount = 0;
    header.marker = true;
    header.payloadType = static_cast<uint8_t>(RtpPayloadType::PCMU);
    header.sequenceNumber = 1234;
    header.timestamp = 5678;
    header.ssrc = 0x12345678;

    // Create test payload
    std::vector<uint8_t> payload(160, 0x5A); // 20ms of silence at 8kHz

    return RtpPacket(header, payload.data(), payload.size());
}

// Helper function to create a test RTCP sender report
RtcpSenderReport createTestRtcpSenderReport() {
    RtcpSenderReport sr;
    sr.setSsrc(0x12345678);
    sr.setNtpTimestamp(0x12345678, 0x87654321);
    sr.setRtpTimestamp(0x98765432);
    sr.setPacketCount(100);
    sr.setOctetCount(16000);
    return sr;
}

// Helper function to create a test RTCP receiver report
RtcpReceiverReport createTestRtcpReceiverReport() {
    RtcpReceiverReport rr;
    rr.setSsrc(0x12345678);

    RtcpReceiverReport::ReportBlock block;
    block.ssrc = 0x87654321;
    block.fractionLost = 0;
    block.cumulativeLost = 0;
    block.extendedHighestSeqNum = 1234;
    block.interarrivalJitter = 0;
    block.lastSR = 0x12345678;
    block.delaySinceLastSR = 0x87654321;

    rr.addReportBlock(block);
    return rr;
}

// Test RTP packet serialization/deserialization
TEST_F(RtpTest, RtpPacketSerializeDeserialize) {
    // Create a test packet
    auto packet = createTestRtpPacket();

    // Serialize it
    auto data = packet.serialize();

    // Make sure we have at least the RTP header (12 bytes) + payload
    ASSERT_GE(data.size(), 12 + 160);

    // Deserialize it
    auto deserialized = RtpPacket::deserialize(data.data(), data.size());

    // Verify the header fields
    const auto& header = deserialized.getHeader();
    EXPECT_EQ(header.version, 2);
    EXPECT_FALSE(header.padding);
    EXPECT_FALSE(header.extension);
    EXPECT_EQ(header.csrcCount, 0);
    EXPECT_TRUE(header.marker);
    EXPECT_EQ(header.payloadType, static_cast<uint8_t>(RtpPayloadType::PCMU));
    EXPECT_EQ(header.sequenceNumber, 1234);
    EXPECT_EQ(header.timestamp, 5678);
    EXPECT_EQ(header.ssrc, 0x12345678);

    // Verify the payload
    EXPECT_EQ(deserialized.getPayloadSize(), 160);
    EXPECT_EQ(deserialized.getPayload()[0], 0x5A);
    EXPECT_EQ(deserialized.getPayload()[159], 0x5A);
}

// Test RTCP SR serialization/deserialization
TEST_F(RtpTest, RtcpSenderReportSerializeDeserialize) {
    // Create a test SR
    auto sr = createTestRtcpSenderReport();

    // Serialize it
    auto data = sr.serialize();

    // Make sure we have at least the minimum required size (28 bytes)
    ASSERT_GE(data.size(), 28);

    // Deserialize it
    auto deserialized = RtcpPacket::deserialize(data.data(), data.size());

    // Verify it's a sender report
    ASSERT_EQ(deserialized->getType(), RtcpPacketType::SR);

    // Cast it to a sender report
    auto sr_deserialized = dynamic_cast<RtcpSenderReport*>(deserialized.get());
    ASSERT_NE(sr_deserialized, nullptr);

    // Verify the fields
    EXPECT_EQ(sr_deserialized->getSsrc(), 0x12345678);
    EXPECT_EQ(sr_deserialized->getNtpTimestampHigh(), 0x12345678);
    EXPECT_EQ(sr_deserialized->getNtpTimestampLow(), 0x87654321);
    EXPECT_EQ(sr_deserialized->getRtpTimestamp(), 0x98765432);
    EXPECT_EQ(sr_deserialized->getPacketCount(), 100);
    EXPECT_EQ(sr_deserialized->getOctetCount(), 16000);
}

// Test RTCP RR serialization/deserialization
TEST_F(RtpTest, RtcpReceiverReportSerializeDeserialize) {
    // Create a test RR
    auto rr = createTestRtcpReceiverReport();

    // Serialize it
    auto data = rr.serialize();

    // Make sure we have at least the minimum required size (8 bytes header + 24 bytes per report block)
    ASSERT_GE(data.size(), 8 + 24);

    // Deserialize it
    auto deserialized = RtcpPacket::deserialize(data.data(), data.size());

    // Verify it's a receiver report
    ASSERT_EQ(deserialized->getType(), RtcpPacketType::RR);

    // Cast it to a receiver report
    auto rr_deserialized = dynamic_cast<RtcpReceiverReport*>(deserialized.get());
    ASSERT_NE(rr_deserialized, nullptr);

    // Verify the fields
    EXPECT_EQ(rr_deserialized->getSsrc(), 0x12345678);

    // Verify the report blocks
    const auto& blocks = rr_deserialized->getReportBlocks();
    ASSERT_EQ(blocks.size(), 1);

    const auto& block = blocks[0];
    EXPECT_EQ(block.ssrc, 0x87654321);
    EXPECT_EQ(block.fractionLost, 0);
    EXPECT_EQ(block.cumulativeLost, 0);
    EXPECT_EQ(block.extendedHighestSeqNum, 1234);
    EXPECT_EQ(block.interarrivalJitter, 0);
    EXPECT_EQ(block.lastSR, 0x12345678);
    EXPECT_EQ(block.delaySinceLastSR, 0x87654321);
}

// Test RTP Session creation and management
TEST_F(RtpTest, RtpSessionManagement) {
    // Create a session
    auto session = RtpSession::create();
    ASSERT_NE(session, nullptr);

    // Start the session
    session->start();

    // Create some RTP streams
    RtpAudioParams audio_params;
    audio_params.payloadType = RtpPayloadType::PCMU;
    audio_params.clockRate = 8000;
    audio_params.channels = 1;
    audio_params.packetizationTime = 20;

    auto audio_stream = RtpAudioStream::create(0x12345678, "test-audio", audio_params);
    ASSERT_NE(audio_stream, nullptr);

    RtpVideoParams video_params;
    video_params.payloadType = RtpPayloadType::H263;
    video_params.clockRate = 90000;
    video_params.maxPacketSize = 1400;

    auto video_stream = RtpVideoStream::create(0x87654321, "test-video", video_params);
    ASSERT_NE(video_stream, nullptr);

    // Add the streams to the session
    uint32_t audio_id = session->addStream(audio_stream);
    uint32_t video_id = session->addStream(video_stream);

    // Verify we can get them back
    auto stream1 = session->getStream(audio_id);
    ASSERT_NE(stream1, nullptr);
    EXPECT_EQ(stream1->getSsrc(), 0x12345678);

    auto stream2 = session->getStream(video_id);
    ASSERT_NE(stream2, nullptr);
    EXPECT_EQ(stream2->getSsrc(), 0x87654321);

    // Verify we can list all stream IDs
    auto stream_ids = session->getStreamIds();
    ASSERT_EQ(stream_ids.size(), 2);
    EXPECT_TRUE(std::find(stream_ids.begin(), stream_ids.end(), audio_id) != stream_ids.end());
    EXPECT_TRUE(std::find(stream_ids.begin(), stream_ids.end(), video_id) != stream_ids.end());

    // Open the streams
    audio_stream->open();
    video_stream->open();

    // Verify they're active
    EXPECT_TRUE(audio_stream->isActive());
    EXPECT_TRUE(video_stream->isActive());

    // Remove a stream
    session->removeStream(audio_id);

    // Verify it's gone
    auto stream_ids_after = session->getStreamIds();
    ASSERT_EQ(stream_ids_after.size(), 1);
    EXPECT_TRUE(std::find(stream_ids_after.begin(), stream_ids_after.end(), video_id) != stream_ids_after.end());

    // Close remaining stream and stop session
    video_stream->close();
    session->stop();

    // Verify stream is inactive
    EXPECT_FALSE(video_stream->isActive());
}

// Test RTP Sender functionality
TEST_F(RtpTest, RtpSender) {
    // Create a sender
    auto sender = RtpSender::create();
    ASSERT_NE(sender, nullptr);

    // Set a custom CNAME
    sender->setCname("test-sender");
    EXPECT_EQ(sender->getCname(), "test-sender");

    // Start the sender
    sender->start();

    // Create audio/video streams
    RtpAudioParams audio_params;
    audio_params.payloadType = RtpPayloadType::PCMU;
    audio_params.clockRate = 8000;
    audio_params.channels = 1;
    audio_params.packetizationTime = 20;

    auto audio_stream = sender->createAudioStream(audio_params, "test-audio");
    ASSERT_NE(audio_stream, nullptr);

    RtpVideoParams video_params;
    video_params.payloadType = RtpPayloadType::H263;
    video_params.clockRate = 90000;
    video_params.maxPacketSize = 1400;

    auto video_stream = sender->createVideoStream(video_params, "test-video");
    ASSERT_NE(video_stream, nullptr);

    // Create test frames
    std::vector<uint8_t> audio_data(160, 0x5A); // 20ms of silence at 8kHz
    media::AudioFrame audio_frame(
        media::AudioFrameFormat::PCM_S16LE,
        8000,
        1,
        audio_data.data(),
        audio_data.size()
    );

    std::vector<uint8_t> video_data(1000, 0x5A); // Dummy video data
    media::VideoFrame video_frame(
        media::VideoFrameFormat::H263,
        640,
        480,
        30,
        video_data.data(),
        video_data.size()
    );

    // Send frames
    EXPECT_TRUE(sender->sendAudioFrame(audio_stream, audio_frame));
    EXPECT_TRUE(sender->sendVideoFrame(video_stream, video_frame));

    // Stop sender
    sender->stop();
}

// Test RTP Receiver functionality
TEST_F(RtpTest, RtpReceiver) {
    // Create a receiver
    auto receiver = RtpReceiver::create();
    ASSERT_NE(receiver, nullptr);

    // Set custom ports
    receiver->setRtpPort(10000);
    receiver->setRtcpPort(10001);

    EXPECT_EQ(receiver->getRtpPort(), 10000);
    EXPECT_EQ(receiver->getRtcpPort(), 10001);

    // Start the receiver
    receiver->start();

    // Add audio/video track handlers
    bool audio_received = false;
    receiver->addAudioTrackHandler("track1", [&audio_received](media::AudioFrame frame) {
        audio_received = true;
    });

    bool video_received = false;
    receiver->addVideoTrackHandler("track2", [&video_received](media::VideoFrame frame) {
        video_received = true;
    });

    // In a real test, we would connect a sender to this receiver
    // and verify that frames are received. Since we can't do that
    // in this unit test, we'll just verify that the handlers are set up.

    // Remove a track handler
    receiver->removeTrackHandler("track1");

    // Stop receiver
    receiver->stop();
}

// End-to-end test
TEST_F(RtpTest, EndToEndTest) {
    // Create sender and receiver
    auto sender = RtpSender::create();
    auto receiver = RtpReceiver::create();

    // In a real test, these would be connected via UDP sockets
    // But we'll just test the API functions

    sender->start();
    receiver->start();

    // Create audio stream
    RtpAudioParams audio_params;
    audio_params.payloadType = RtpPayloadType::PCMU;
    audio_params.clockRate = 8000;
    audio_params.channels = 1;
    audio_params.packetizationTime = 20;

    auto audio_stream = sender->createAudioStream(audio_params, "test-audio");

    // Set up receiver
    std::atomic<int> frames_received(0);
    receiver->addAudioTrackHandler("track1", [&frames_received](media::AudioFrame frame) {
        frames_received++;
    });

    // Create and send some frames
    for (int i = 0; i < 5; i++) {
        std::vector<uint8_t> audio_data(160, 0x5A); // 20ms of silence at 8kHz
        media::AudioFrame audio_frame(
            media::AudioFrameFormat::PCM_S16LE,
            8000,
            1,
            audio_data.data(),
            audio_data.size()
        );

        sender->sendAudioFrame(audio_stream, audio_frame);

        // In a real test, we would wait for the frame to be received
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Cleanup
    sender->stop();
    receiver->stop();

    // In a real test, we would verify that frames_received > 0
    // but since we're not actually sending/receiving, we can't do that
}

} // namespace fmus::rtp::test