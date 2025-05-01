#include <fmus/media/media.hpp>
#include <fmus/core/logger.hpp>

#include <unordered_map>
#include <mutex>

namespace fmus::media {

// Audio codec implementation
class AudioCodecImpl : public AudioCodec {
public:
    explicit AudioCodecImpl(const AudioCodecParams& params)
        : params_(params) {}

    // Implementation of MediaCodec interface
    std::string name() const override {
        return params_.codec_name;
    }

    // Implementation of AudioCodec interface
    core::Task<std::unique_ptr<AudioFrame>> encode(
        const AudioFrame& frame) override {
        // Pastikan parameter frame sesuai dengan codec
        if (frame.sampleRate() != params_.sample_rate ||
            frame.channels() != params_.channels ||
            frame.format() != params_.sample_format) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Frame parameters do not match codec parameters");
        }

        // Di implementasi sebenarnya, kita akan melakukan encoding di sini
        // Untuk sekarang, kita hanya membuat clone dari frame
        // TODO: Implement actual encoding
        co_return frame.clone();
    }

    core::Task<std::unique_ptr<AudioFrame>> decode(
        const AudioFrame& frame) override {
        // Di implementasi sebenarnya, kita akan melakukan decoding di sini
        // Untuk sekarang, kita hanya membuat clone dari frame
        // TODO: Implement actual decoding
        co_return frame.clone();
    }

    AudioCodecParams params() const override {
        return params_;
    }

private:
    AudioCodecParams params_;
};

// Video codec implementation
class VideoCodecImpl : public VideoCodec {
public:
    explicit VideoCodecImpl(const VideoCodecParams& params)
        : params_(params) {}

    // Implementation of MediaCodec interface
    std::string name() const override {
        return params_.codec_name;
    }

    // Implementation of VideoCodec interface
    core::Task<std::unique_ptr<VideoFrame>> encode(
        const VideoFrame& frame) override {
        // Pastikan parameter frame sesuai dengan codec
        if (frame.width() != params_.width ||
            frame.height() != params_.height ||
            frame.format() != params_.pixel_format) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Frame parameters do not match codec parameters");
        }

        // Di implementasi sebenarnya, kita akan melakukan encoding di sini
        // Untuk sekarang, kita hanya membuat clone dari frame
        // TODO: Implement actual encoding
        co_return frame.clone();
    }

    core::Task<std::unique_ptr<VideoFrame>> decode(
        const VideoFrame& frame) override {
        // Di implementasi sebenarnya, kita akan melakukan decoding di sini
        // Untuk sekarang, kita hanya membuat clone dari frame
        // TODO: Implement actual decoding
        co_return frame.clone();
    }

    VideoCodecParams params() const override {
        return params_;
    }

private:
    VideoCodecParams params_;
};

// Codec registry
namespace {
    // Maps untuk menyimpan codec factories
    using AudioCodecFactory = std::function<std::unique_ptr<AudioCodec>(const AudioCodecParams&)>;
    using VideoCodecFactory = std::function<std::unique_ptr<VideoCodec>(const VideoCodecParams&)>;

    std::unordered_map<std::string, AudioCodecFactory> g_audio_encoders;
    std::unordered_map<std::string, AudioCodecFactory> g_audio_decoders;
    std::unordered_map<std::string, VideoCodecFactory> g_video_encoders;
    std::unordered_map<std::string, VideoCodecFactory> g_video_decoders;
    std::mutex g_codec_mutex;

    // Default codec factories
    std::unique_ptr<AudioCodec> createDefaultAudioCodec(const AudioCodecParams& params) {
        return std::make_unique<AudioCodecImpl>(params);
    }

    std::unique_ptr<VideoCodec> createDefaultVideoCodec(const VideoCodecParams& params) {
        return std::make_unique<VideoCodecImpl>(params);
    }

    // Inisialisasi registry dengan codec default
    struct CodecRegistryInitializer {
        CodecRegistryInitializer() {
            // Register default audio codecs
            g_audio_encoders["pcm"] = createDefaultAudioCodec;
            g_audio_decoders["pcm"] = createDefaultAudioCodec;

            // Register default video codecs (RGB/YUV passthrough)
            g_video_encoders["raw"] = createDefaultVideoCodec;
            g_video_decoders["raw"] = createDefaultVideoCodec;
        }
    };

    // Static initializer untuk register codec default
    static CodecRegistryInitializer g_initializer;
}

// Register custom codec
void registerAudioEncoder(const std::string& name,
                          std::function<std::unique_ptr<AudioCodec>(const AudioCodecParams&)> factory) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    g_audio_encoders[name] = std::move(factory);
}

void registerAudioDecoder(const std::string& name,
                          std::function<std::unique_ptr<AudioCodec>(const AudioCodecParams&)> factory) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    g_audio_decoders[name] = std::move(factory);
}

void registerVideoEncoder(const std::string& name,
                          std::function<std::unique_ptr<VideoCodec>(const VideoCodecParams&)> factory) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    g_video_encoders[name] = std::move(factory);
}

void registerVideoDecoder(const std::string& name,
                          std::function<std::unique_ptr<VideoCodec>(const VideoCodecParams&)> factory) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);
    g_video_decoders[name] = std::move(factory);
}

// MediaCodec factory implementations
std::unique_ptr<MediaCodec> MediaCodec::createAudioEncoder(const AudioCodecParams& params) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);

    auto it = g_audio_encoders.find(params.codec_name);
    if (it == g_audio_encoders.end()) {
        throw MediaError(MediaErrorCode::CodecNotSupported,
            "Audio encoder not supported: " + params.codec_name);
    }

    return it->second(params);
}

std::unique_ptr<MediaCodec> MediaCodec::createAudioDecoder(const AudioCodecParams& params) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);

    auto it = g_audio_decoders.find(params.codec_name);
    if (it == g_audio_decoders.end()) {
        throw MediaError(MediaErrorCode::CodecNotSupported,
            "Audio decoder not supported: " + params.codec_name);
    }

    return it->second(params);
}

std::unique_ptr<MediaCodec> MediaCodec::createVideoEncoder(const VideoCodecParams& params) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);

    auto it = g_video_encoders.find(params.codec_name);
    if (it == g_video_encoders.end()) {
        throw MediaError(MediaErrorCode::CodecNotSupported,
            "Video encoder not supported: " + params.codec_name);
    }

    return it->second(params);
}

std::unique_ptr<MediaCodec> MediaCodec::createVideoDecoder(const VideoCodecParams& params) {
    std::lock_guard<std::mutex> lock(g_codec_mutex);

    auto it = g_video_decoders.find(params.codec_name);
    if (it == g_video_decoders.end()) {
        throw MediaError(MediaErrorCode::CodecNotSupported,
            "Video decoder not supported: " + params.codec_name);
    }

    return it->second(params);
}

} // namespace fmus::media