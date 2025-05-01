#include <gtest/gtest.h>
#include <fmus/media/media.hpp>
#include <fmus/core/task.hpp>

namespace fmus::media::test {

class MediaTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = core::make_task_scheduler();
        scheduler_->start();
    }

    void TearDown() override {
        scheduler_->stop();
        scheduler_.reset();
    }

    std::unique_ptr<core::TaskScheduler> scheduler_;
};

// Test audio frame
TEST_F(MediaTest, AudioFrame) {
    // Create frame with specified parameters
    const uint32_t sample_rate = 44100;
    const uint8_t channels = 2;
    const size_t num_samples = 1024;
    AudioFrame frame(AudioSampleFormat::S16, sample_rate, channels, num_samples);

    // Verify frame properties
    EXPECT_EQ(frame.format(), AudioSampleFormat::S16);
    EXPECT_EQ(frame.sampleRate(), sample_rate);
    EXPECT_EQ(frame.channels(), channels);
    EXPECT_EQ(frame.numSamples(), num_samples);
    EXPECT_EQ(frame.size(), num_samples * channels * 2);  // 2 bytes per sample (S16)

    // Test constructing from data
    std::vector<int16_t> test_data(num_samples * channels, 1000);  // Fill with test value
    AudioFrame frame2(test_data.data(), test_data.size() * sizeof(int16_t),
                    AudioSampleFormat::S16, sample_rate, channels);

    EXPECT_EQ(frame2.numSamples(), num_samples);
    EXPECT_EQ(frame2.size(), test_data.size() * sizeof(int16_t));

    // Verify data was copied correctly
    const int16_t* frame_data = reinterpret_cast<const int16_t*>(frame2.data());
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_EQ(frame_data[i], test_data[i]);
    }

    // Test duration calculation
    auto duration = frame.duration();
    EXPECT_EQ(duration.count(), 1000000ULL * num_samples / sample_rate);

    // Test clone
    auto frame_clone = frame.clone();
    EXPECT_EQ(frame_clone->format(), frame.format());
    EXPECT_EQ(frame_clone->sampleRate(), frame.sampleRate());
    EXPECT_EQ(frame_clone->channels(), frame.channels());
    EXPECT_EQ(frame_clone->numSamples(), frame.numSamples());
    EXPECT_EQ(frame_clone->size(), frame.size());
}

// Test video frame
TEST_F(MediaTest, VideoFrame) {
    // Create frame with specified parameters
    const uint32_t width = 1280;
    const uint32_t height = 720;
    VideoFrame frame(VideoPixelFormat::RGB24, width, height);

    // Verify frame properties
    EXPECT_EQ(frame.format(), VideoPixelFormat::RGB24);
    EXPECT_EQ(frame.width(), width);
    EXPECT_EQ(frame.height(), height);
    EXPECT_EQ(frame.size(), width * height * 3);  // 3 bytes per pixel (RGB24)

    // Test constructing from data
    std::vector<uint8_t> test_data(width * height * 3, 100);  // Fill with test value
    VideoFrame frame2(test_data.data(), test_data.size(),
                   VideoPixelFormat::RGB24, width, height);

    EXPECT_EQ(frame2.size(), test_data.size());

    // Verify data was copied correctly
    const uint8_t* frame_data = frame2.data();
    for (size_t i = 0; i < test_data.size(); ++i) {
        EXPECT_EQ(frame_data[i], test_data[i]);
    }

    // Test clone
    auto frame_clone = frame.clone();
    EXPECT_EQ(frame_clone->format(), frame.format());
    EXPECT_EQ(frame_clone->width(), frame.width());
    EXPECT_EQ(frame_clone->height(), frame.height());
    EXPECT_EQ(frame_clone->size(), frame.size());
}

// Test audio codec
TEST_F(MediaTest, AudioCodec) {
    // Create codec parameters
    AudioCodecParams params;
    params.codec_name = "pcm";
    params.sample_rate = 44100;
    params.channels = 2;
    params.sample_format = AudioSampleFormat::S16;

    // Create encoder
    auto encoder = dynamic_cast<AudioCodec*>(
        MediaCodec::createAudioEncoder(params).release());
    ASSERT_NE(encoder, nullptr);
    std::unique_ptr<AudioCodec> encoder_ptr(encoder);

    // Verify codec properties
    EXPECT_EQ(encoder->type(), MediaType::Audio);
    EXPECT_EQ(encoder->name(), params.codec_name);

    // Create sample frame
    const size_t num_samples = 1024;
    AudioFrame frame(params.sample_format, params.sample_rate,
                   params.channels, num_samples);

    // Test encoding
    auto encode_task = [&]() -> core::Task<void> {
        auto encoded_frame = co_await encoder->encode(frame);
        EXPECT_NE(encoded_frame, nullptr);
    }();

    scheduler_->runSync(encode_task);
}

// Test video codec
TEST_F(MediaTest, VideoCodec) {
    // Create codec parameters
    VideoCodecParams params;
    params.codec_name = "raw";
    params.width = 1280;
    params.height = 720;
    params.frame_rate = 30.0f;
    params.pixel_format = VideoPixelFormat::YUV420P;

    // Create encoder
    auto encoder = dynamic_cast<VideoCodec*>(
        MediaCodec::createVideoEncoder(params).release());
    ASSERT_NE(encoder, nullptr);
    std::unique_ptr<VideoCodec> encoder_ptr(encoder);

    // Verify codec properties
    EXPECT_EQ(encoder->type(), MediaType::Video);
    EXPECT_EQ(encoder->name(), params.codec_name);

    // Create sample frame
    VideoFrame frame(params.pixel_format, params.width, params.height);

    // Test encoding
    auto encode_task = [&]() -> core::Task<void> {
        auto encoded_frame = co_await encoder->encode(frame);
        EXPECT_NE(encoded_frame, nullptr);
    }();

    scheduler_->runSync(encode_task);
}

// Test media device enumeration
TEST_F(MediaTest, DeviceEnumeration) {
    auto enum_task = [this]() -> core::Task<void> {
        auto devices = co_await MediaDevice::enumerateDevices();

        // Should have at least one device of each type
        bool has_audio_input = false;
        bool has_audio_output = false;
        bool has_video_input = false;

        for (const auto& device : devices) {
            // Each device should have a non-empty ID and name
            EXPECT_FALSE(device->id().empty());
            EXPECT_FALSE(device->name().empty());

            // Check device type
            switch (device->type()) {
                case MediaDeviceType::AudioInput:
                    has_audio_input = true;
                    break;
                case MediaDeviceType::AudioOutput:
                    has_audio_output = true;
                    break;
                case MediaDeviceType::VideoInput:
                    has_video_input = true;
                    break;
                default:
                    break;
            }

            // Device should report capabilities
            auto caps = device->capabilities();
            if (device->type() == MediaDeviceType::AudioInput ||
                device->type() == MediaDeviceType::AudioOutput) {
                EXPECT_FALSE(caps.sample_rates.empty());
                EXPECT_FALSE(caps.channels.empty());
            } else if (device->type() == MediaDeviceType::VideoInput) {
                EXPECT_FALSE(caps.resolutions.empty());
                EXPECT_FALSE(caps.frame_rates.empty());
            }
        }

        EXPECT_TRUE(has_audio_input);
        EXPECT_TRUE(has_audio_output);
        EXPECT_TRUE(has_video_input);
    }();

    scheduler_->runSync(enum_task);
}

// Test media stream
TEST_F(MediaTest, MediaStream) {
    // Create empty stream
    auto stream = MediaStream::create();
    EXPECT_FALSE(stream->isActive());

    // Add audio track
    AudioCodecParams audio_params;
    audio_params.codec_name = "pcm";
    audio_params.sample_rate = 44100;
    audio_params.channels = 2;
    audio_params.sample_format = AudioSampleFormat::S16;

    auto audio_track = createAudioTrack("audio-1", audio_params);
    stream->addTrack(audio_track);

    // Add video track
    VideoCodecParams video_params;
    video_params.codec_name = "raw";
    video_params.width = 1280;
    video_params.height = 720;
    video_params.frame_rate = 30.0f;
    video_params.pixel_format = VideoPixelFormat::YUV420P;

    auto video_track = createVideoTrack("video-1", video_params);
    stream->addTrack(video_track);

    // Verify tracks
    auto tracks = stream->getTracks();
    EXPECT_EQ(tracks.size(), 2);

    // Get tracks by ID
    auto audio_track2 = stream->getTrackById("audio-1");
    EXPECT_EQ(audio_track2, audio_track);

    auto video_track2 = stream->getTrackById("video-1");
    EXPECT_EQ(video_track2, video_track);

    // Enable/disable tracks
    audio_track->enable(false);
    EXPECT_FALSE(audio_track->isEnabled());

    audio_track->enable(true);
    EXPECT_TRUE(audio_track->isEnabled());

    // Start/stop stream
    stream->start();
    EXPECT_TRUE(stream->isActive());

    stream->stop();
    EXPECT_FALSE(stream->isActive());

    // Remove track
    stream->removeTrack("audio-1");
    tracks = stream->getTracks();
    EXPECT_EQ(tracks.size(), 1);
    EXPECT_EQ(tracks[0], video_track);
}

// Test device source
TEST_F(MediaTest, DeviceSource) {
    auto source_task = [this]() -> core::Task<void> {
        // Enumerate devices
        auto devices = co_await MediaDevice::enumerateDevices();
        ASSERT_FALSE(devices.empty());

        // Find a video input device
        std::unique_ptr<MediaDevice> video_device;
        for (auto& device : devices) {
            if (device->type() == MediaDeviceType::VideoInput) {
                video_device = std::move(device);
                break;
            }
        }

        ASSERT_NE(video_device, nullptr);

        // Create device source
        auto source = MediaSource::createDeviceSource(std::move(video_device));
        ASSERT_NE(source, nullptr);

        // Open source
        co_await source->open();

        // Get stream
        auto stream = source->stream();
        ASSERT_NE(stream, nullptr);
        EXPECT_TRUE(stream->isActive());

        // Verify stream has tracks
        auto tracks = stream->getTracks();
        EXPECT_FALSE(tracks.empty());

        // Close source
        source->close();
        EXPECT_FALSE(stream->isActive());
    }();

    scheduler_->runSync(source_task);
}

// Helper untuk membuat media tracks (eksposisi fungsi untuk test saja)
extern std::shared_ptr<AudioTrack> createAudioTrack(std::string id,
                                                   const AudioCodecParams& params);
extern std::shared_ptr<VideoTrack> createVideoTrack(std::string id,
                                                   const VideoCodecParams& params);

} // namespace fmus::media::test