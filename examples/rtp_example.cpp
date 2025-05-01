#include <fmus/rtp/rtp.hpp>
#include <fmus/media/media.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <memory>
#include <csignal>

using namespace fmus;

// Global variables to handle termination
std::atomic<bool> g_running = true;

// Signal handler for clean shutdown
void signalHandler(int signal) {
    core::Logger::info("Received signal {}, terminating...", signal);
    g_running = false;
}

// Simulate generating audio frames (in a real application, these would come from a source like a microphone)
media::AudioFrame generateAudioFrame(uint32_t sample_rate, uint8_t channels) {
    // Generate 20ms of silent audio
    uint32_t samples_per_frame = sample_rate * 20 / 1000;
    uint32_t bytes_per_sample = channels * 2; // 16-bit samples (2 bytes) per channel
    uint32_t frame_size = samples_per_frame * bytes_per_sample;

    std::vector<uint8_t> data(frame_size, 0);

    return media::AudioFrame(
        media::AudioFrameFormat::PCM_S16LE,
        sample_rate,
        channels,
        data.data(),
        data.size()
    );
}

// Simulate generating video frames (in a real application, these would come from a source like a camera)
media::VideoFrame generateVideoFrame(uint32_t width, uint32_t height, uint32_t frame_rate) {
    // Generate a dummy video frame
    uint32_t frame_size = width * height * 3 / 2; // YUV420 format
    std::vector<uint8_t> data(frame_size, 0);

    // Fill with a simple pattern (not real video data, just for illustration)
    for (uint32_t i = 0; i < width * height; i++) {
        data[i] = i % 255; // Y (luma)
    }

    return media::VideoFrame(
        media::VideoFrameFormat::YUV420P,
        width,
        height,
        frame_rate,
        data.data(),
        data.size()
    );
}

// Example of RTP sender
void runRtpSender() {
    // Create and configure RTP sender
    auto rtp_sender = rtp::RtpSender::create();
    rtp_sender->setCname("fmus-rtp-example");

    // Start RTP sender
    rtp_sender->start();

    // Create audio and video streams
    rtp::RtpAudioParams audio_params;
    audio_params.payloadType = rtp::RtpPayloadType::PCMU;
    audio_params.clockRate = 48000;
    audio_params.channels = 2;
    audio_params.packetizationTime = 20;

    auto audio_stream = rtp_sender->createAudioStream(audio_params);

    rtp::RtpVideoParams video_params;
    video_params.payloadType = rtp::RtpPayloadType::H264;
    video_params.clockRate = 90000;
    video_params.maxPacketSize = 1400;

    auto video_stream = rtp_sender->createVideoStream(video_params);

    core::Logger::info("RTP Sender started with audio SSRC {} and video SSRC {}",
                     audio_stream->getSsrc(), video_stream->getSsrc());

    // Main loop for sending frames
    uint32_t frame_count = 0;
    auto next_video_time = std::chrono::steady_clock::now();

    while (g_running) {
        // Generate and send audio frames at regular intervals (20ms)
        auto audio_frame = generateAudioFrame(audio_params.clockRate, audio_params.channels);
        rtp_sender->sendAudioFrame(audio_stream, audio_frame);

        // Generate and send video frames at regular intervals (e.g., 30fps)
        auto now = std::chrono::steady_clock::now();
        if (now >= next_video_time) {
            auto video_frame = generateVideoFrame(1280, 720, 30);
            rtp_sender->sendVideoFrame(video_stream, video_frame);

            // Schedule next video frame
            next_video_time = now + std::chrono::milliseconds(33); // ~30fps
            frame_count++;

            // Log every 30 frames (approximately once per second)
            if (frame_count % 30 == 0) {
                core::Logger::info("Sent {} video frames", frame_count);
            }
        }

        // Sleep a short time to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Stop RTP sender
    rtp_sender->stop();
    core::Logger::info("RTP Sender stopped");
}

// Example of RTP receiver
void runRtpReceiver() {
    // Create and configure RTP receiver
    auto rtp_receiver = rtp::RtpReceiver::create();

    // Set ports (in real applications, these should be configurable)
    rtp_receiver->setRtpPort(10000);
    rtp_receiver->setRtcpPort(10001);

    // Register handlers for audio and video tracks
    rtp_receiver->addAudioTrackHandler("default", [](media::AudioFrame frame) {
        core::Logger::debug("Received audio frame: sampleRate={}, channels={}, size={}",
                          frame.getSampleRate(), frame.getChannels(), frame.getSize());

        // Process the audio frame (e.g., play it, save it, etc.)
    });

    rtp_receiver->addVideoTrackHandler("default", [](media::VideoFrame frame) {
        core::Logger::debug("Received video frame: {}x{}, format={}, size={}",
                          frame.getWidth(), frame.getHeight(),
                          static_cast<int>(frame.getFormat()), frame.getSize());

        // Process the video frame (e.g., display it, save it, etc.)
    });

    // Start RTP receiver
    rtp_receiver->start();
    core::Logger::info("RTP Receiver started on ports {}/{}",
                     rtp_receiver->getRtpPort(), rtp_receiver->getRtcpPort());

    // Wait until program termination
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop RTP receiver
    rtp_receiver->stop();
    core::Logger::info("RTP Receiver stopped");
}

int main(int argc, char* argv[]) {
    // Initialize logger
    core::Logger::init(core::LogLevel::Info);

    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize task scheduler
    auto& scheduler = core::TaskScheduler::getInstance();
    scheduler.start(4); // Use 4 worker threads

    // Determine whether to run as sender or receiver based on command line argument
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [sender|receiver]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "sender") {
        runRtpSender();
    } else if (mode == "receiver") {
        runRtpReceiver();
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        std::cerr << "Usage: " << argv[0] << " [sender|receiver]" << std::endl;
        return 1;
    }

    // Stop task scheduler
    scheduler.stop();

    return 0;
}