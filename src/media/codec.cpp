#include "fmus/media/codec.hpp"
#include "fmus/core/logger.hpp"
#include <algorithm>
#include <cmath>

namespace fmus::media {

// G.711 μ-law and A-law implementation
namespace g711 {

// μ-law compression tables
static const int16_t MULAW_BIAS = 0x84;
static const int16_t MULAW_CLIP = 32635;

// A-law compression constants
static const int16_t ALAW_AMI_MASK = 0x55;

// μ-law encode
uint8_t mulaw_encode(int16_t sample) {
    int16_t sign = (sample >> 8) & 0x80;
    if (sign != 0) sample = -sample;
    if (sample > MULAW_CLIP) sample = MULAW_CLIP;
    
    sample += MULAW_BIAS;
    int16_t exponent = 7;
    for (int16_t exp_mask = 0x4000; (sample & exp_mask) == 0 && exponent > 0; exponent--, exp_mask >>= 1);
    
    int16_t mantissa = (sample >> (exponent + 3)) & 0x0F;
    uint8_t mulaw_byte = ~(sign | (exponent << 4) | mantissa);
    
    return mulaw_byte;
}

// μ-law decode
int16_t mulaw_decode(uint8_t mulaw_byte) {
    mulaw_byte = ~mulaw_byte;
    int16_t sign = mulaw_byte & 0x80;
    int16_t exponent = (mulaw_byte >> 4) & 0x07;
    int16_t mantissa = mulaw_byte & 0x0F;
    
    int16_t sample = mantissa << (exponent + 3);
    sample += (0x84 << exponent);
    sample -= MULAW_BIAS;
    
    return sign != 0 ? -sample : sample;
}

// A-law encode
uint8_t alaw_encode(int16_t sample) {
    int16_t sign = sample & 0x8000;
    if (sign != 0) sample = -sample;
    if (sample > 32635) sample = 32635;
    
    uint8_t alaw_byte;
    if (sample < 256) {
        alaw_byte = sample >> 4;
    } else {
        int16_t exponent = 7;
        for (int16_t exp_mask = 0x4000; (sample & exp_mask) == 0 && exponent > 0; exponent--, exp_mask >>= 1);
        
        int16_t mantissa = (sample >> (exponent + 3)) & 0x0F;
        alaw_byte = ((exponent << 4) | mantissa);
    }
    
    return sign != 0 ? (ALAW_AMI_MASK | alaw_byte) : (ALAW_AMI_MASK & ~alaw_byte);
}

// A-law decode
int16_t alaw_decode(uint8_t alaw_byte) {
    alaw_byte ^= ALAW_AMI_MASK;
    int16_t sign = alaw_byte & 0x80;
    int16_t exponent = (alaw_byte >> 4) & 0x07;
    int16_t mantissa = alaw_byte & 0x0F;
    
    int16_t sample;
    if (exponent == 0) {
        sample = (mantissa << 4) + 8;
    } else {
        sample = ((mantissa << 4) + 0x108) << (exponent - 1);
    }
    
    return sign != 0 ? -sample : sample;
}

} // namespace g711

// PCMU (G.711 μ-law) Encoder
class PcmuEncoder : public AudioEncoder {
public:
    PcmuEncoder() : configured_(false), payload_type_(0) {}
    
    std::string getName() const override { return "PCMU"; }
    uint8_t getPayloadType() const override { return payload_type_; }
    
    bool configure(const CodecParameters& params) override {
        if (params.sample_rate != 8000 || params.channels != 1) {
            core::Logger::error("PCMU only supports 8kHz mono");
            return false;
        }
        
        params_ = params;
        payload_type_ = static_cast<uint8_t>(AudioCodecId::PCMU);
        configured_ = true;
        return true;
    }
    
    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }
    
    std::vector<uint8_t> encode(const AudioFrame& frame) override {
        std::vector<uint8_t> output;
        encode(frame, output);
        return output;
    }
    
    bool encode(const AudioFrame& frame, std::vector<uint8_t>& output) override {
        if (!configured_) return false;
        
        const auto& data = frame.getData();
        size_t sample_count = data.size() / 2; // 16-bit samples
        
        output.resize(sample_count);
        
        for (size_t i = 0; i < sample_count; ++i) {
            int16_t sample = static_cast<int16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            output[i] = g711::mulaw_encode(sample);
        }
        
        return true;
    }
    
    size_t getFrameSize() const override { return 160; } // 20ms at 8kHz
    size_t getEncodedFrameSize() const override { return 160; } // 1 byte per sample

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// PCMU (G.711 μ-law) Decoder
class PcmuDecoder : public AudioDecoder {
public:
    PcmuDecoder() : configured_(false), payload_type_(0) {}
    
    std::string getName() const override { return "PCMU"; }
    uint8_t getPayloadType() const override { return payload_type_; }
    
    bool configure(const CodecParameters& params) override {
        if (params.sample_rate != 8000 || params.channels != 1) {
            core::Logger::error("PCMU only supports 8kHz mono");
            return false;
        }
        
        params_ = params;
        payload_type_ = static_cast<uint8_t>(AudioCodecId::PCMU);
        configured_ = true;
        return true;
    }
    
    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }
    
    AudioFrame decode(const std::vector<uint8_t>& data) override {
        AudioFrame frame;
        decode(data, frame);
        return frame;
    }
    
    bool decode(const std::vector<uint8_t>& data, AudioFrame& frame) override {
        if (!configured_) return false;
        
        std::vector<uint8_t> pcm_data(data.size() * 2); // 16-bit output
        
        for (size_t i = 0; i < data.size(); ++i) {
            int16_t sample = g711::mulaw_decode(data[i]);
            pcm_data[i * 2] = sample & 0xFF;
            pcm_data[i * 2 + 1] = (sample >> 8) & 0xFF;
        }
        
        frame = AudioFrame(pcm_data, 8000, 1);
        return true;
    }
    
    size_t getFrameSize() const override { return 160; } // 20ms at 8kHz
    size_t getDecodedFrameSize() const override { return 320; } // 2 bytes per sample

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// PCMA (G.711 A-law) Encoder
class PcmaEncoder : public AudioEncoder {
public:
    PcmaEncoder() : configured_(false), payload_type_(8) {}
    
    std::string getName() const override { return "PCMA"; }
    uint8_t getPayloadType() const override { return payload_type_; }
    
    bool configure(const CodecParameters& params) override {
        if (params.sample_rate != 8000 || params.channels != 1) {
            core::Logger::error("PCMA only supports 8kHz mono");
            return false;
        }
        
        params_ = params;
        payload_type_ = static_cast<uint8_t>(AudioCodecId::PCMA);
        configured_ = true;
        return true;
    }
    
    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }
    
    std::vector<uint8_t> encode(const AudioFrame& frame) override {
        std::vector<uint8_t> output;
        encode(frame, output);
        return output;
    }
    
    bool encode(const AudioFrame& frame, std::vector<uint8_t>& output) override {
        if (!configured_) return false;
        
        const auto& data = frame.getData();
        size_t sample_count = data.size() / 2; // 16-bit samples
        
        output.resize(sample_count);
        
        for (size_t i = 0; i < sample_count; ++i) {
            int16_t sample = static_cast<int16_t>(data[i * 2] | (data[i * 2 + 1] << 8));
            output[i] = g711::alaw_encode(sample);
        }
        
        return true;
    }
    
    size_t getFrameSize() const override { return 160; } // 20ms at 8kHz
    size_t getEncodedFrameSize() const override { return 160; } // 1 byte per sample

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// PCMA (G.711 A-law) Decoder
class PcmaDecoder : public AudioDecoder {
public:
    PcmaDecoder() : configured_(false), payload_type_(8) {}
    
    std::string getName() const override { return "PCMA"; }
    uint8_t getPayloadType() const override { return payload_type_; }
    
    bool configure(const CodecParameters& params) override {
        if (params.sample_rate != 8000 || params.channels != 1) {
            core::Logger::error("PCMA only supports 8kHz mono");
            return false;
        }
        
        params_ = params;
        payload_type_ = static_cast<uint8_t>(AudioCodecId::PCMA);
        configured_ = true;
        return true;
    }
    
    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }
    
    AudioFrame decode(const std::vector<uint8_t>& data) override {
        AudioFrame frame;
        decode(data, frame);
        return frame;
    }
    
    bool decode(const std::vector<uint8_t>& data, AudioFrame& frame) override {
        if (!configured_) return false;
        
        std::vector<uint8_t> pcm_data(data.size() * 2); // 16-bit output
        
        for (size_t i = 0; i < data.size(); ++i) {
            int16_t sample = g711::alaw_decode(data[i]);
            pcm_data[i * 2] = sample & 0xFF;
            pcm_data[i * 2 + 1] = (sample >> 8) & 0xFF;
        }
        
        frame = AudioFrame(pcm_data, 8000, 1);
        return true;
    }
    
    size_t getFrameSize() const override { return 160; } // 20ms at 8kHz
    size_t getDecodedFrameSize() const override { return 320; } // 2 bytes per sample

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// Stub Video Encoder (for basic H.264 support)
class H264Encoder : public VideoEncoder {
public:
    H264Encoder() : configured_(false), payload_type_(96) {}

    std::string getName() const override { return "H264"; }
    uint8_t getPayloadType() const override { return payload_type_; }

    bool configure(const CodecParameters& params) override {
        params_ = params;
        payload_type_ = static_cast<uint8_t>(VideoCodecId::H264);
        configured_ = true;
        return true;
    }

    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }

    std::vector<uint8_t> encode(const VideoFrame& frame, bool keyframe) override {
        // Stub implementation - returns dummy H.264 NAL unit
        std::vector<uint8_t> output;
        output.push_back(0x00); output.push_back(0x00); output.push_back(0x00); output.push_back(0x01); // Start code
        output.push_back(keyframe ? 0x67 : 0x41); // NAL header (SPS for keyframe, P-frame otherwise)

        // Add dummy payload based on frame size
        size_t payload_size = frame.getSize() / 10; // Simulate compression
        output.resize(5 + payload_size, 0x42);

        return output;
    }

    bool encode(const VideoFrame& frame, std::vector<uint8_t>& output, bool keyframe) override {
        output = encode(frame, keyframe);
        return true;
    }

    void requestKeyframe() override {
        // Implementation would request keyframe from encoder
    }

    bool isKeyframe(const std::vector<uint8_t>& data) const override {
        // Check NAL unit type for keyframe
        if (data.size() >= 5) {
            uint8_t nal_type = data[4] & 0x1F;
            return nal_type == 5 || nal_type == 7; // IDR or SPS
        }
        return false;
    }

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// Stub Video Decoder (for basic H.264 support)
class H264Decoder : public VideoDecoder {
public:
    H264Decoder() : configured_(false), payload_type_(96) {}

    std::string getName() const override { return "H264"; }
    uint8_t getPayloadType() const override { return payload_type_; }

    bool configure(const CodecParameters& params) override {
        params_ = params;
        payload_type_ = static_cast<uint8_t>(VideoCodecId::H264);
        configured_ = true;
        return true;
    }

    CodecParameters getParameters() const override { return params_; }
    bool isConfigured() const override { return configured_; }
    void reset() override { configured_ = false; }

    VideoFrame decode(const std::vector<uint8_t>& data) override {
        // Stub implementation - returns dummy RGB frame
        uint16_t width = params_.width > 0 ? params_.width : 320;
        uint16_t height = params_.height > 0 ? params_.height : 240;

        std::vector<uint8_t> rgb_data(width * height * 3, 0x80); // Gray frame
        return VideoFrame(rgb_data, width, height);
    }

    bool decode(const std::vector<uint8_t>& data, VideoFrame& frame) override {
        frame = decode(data);
        return true;
    }

    bool isKeyframe(const std::vector<uint8_t>& data) const override {
        // Check NAL unit type for keyframe
        if (data.size() >= 5) {
            uint8_t nal_type = data[4] & 0x1F;
            return nal_type == 5 || nal_type == 7; // IDR or SPS
        }
        return false;
    }

private:
    bool configured_;
    uint8_t payload_type_;
    CodecParameters params_;
};

// CodecFactory implementation
std::unique_ptr<AudioEncoder> CodecFactory::createAudioEncoder(AudioCodecId codec_id) {
    switch (codec_id) {
        case AudioCodecId::PCMU:
            return std::make_unique<PcmuEncoder>();
        case AudioCodecId::PCMA:
            return std::make_unique<PcmaEncoder>();
        default:
            core::Logger::error("Unsupported audio encoder: {}", static_cast<int>(codec_id));
            return nullptr;
    }
}

std::unique_ptr<AudioDecoder> CodecFactory::createAudioDecoder(AudioCodecId codec_id) {
    switch (codec_id) {
        case AudioCodecId::PCMU:
            return std::make_unique<PcmuDecoder>();
        case AudioCodecId::PCMA:
            return std::make_unique<PcmaDecoder>();
        default:
            core::Logger::error("Unsupported audio decoder: {}", static_cast<int>(codec_id));
            return nullptr;
    }
}

std::unique_ptr<VideoEncoder> CodecFactory::createVideoEncoder(VideoCodecId codec_id) {
    switch (codec_id) {
        case VideoCodecId::H264:
            return std::make_unique<H264Encoder>();
        default:
            core::Logger::error("Unsupported video encoder: {}", static_cast<int>(codec_id));
            return nullptr;
    }
}

std::unique_ptr<VideoDecoder> CodecFactory::createVideoDecoder(VideoCodecId codec_id) {
    switch (codec_id) {
        case VideoCodecId::H264:
            return std::make_unique<H264Decoder>();
        default:
            core::Logger::error("Unsupported video decoder: {}", static_cast<int>(codec_id));
            return nullptr;
    }
}

std::string CodecFactory::getCodecName(AudioCodecId codec_id) {
    switch (codec_id) {
        case AudioCodecId::PCMU: return "PCMU";
        case AudioCodecId::PCMA: return "PCMA";
        case AudioCodecId::G722: return "G722";
        case AudioCodecId::G729: return "G729";
        case AudioCodecId::OPUS: return "OPUS";
        default: return "UNKNOWN";
    }
}

std::string CodecFactory::getCodecName(VideoCodecId codec_id) {
    switch (codec_id) {
        case VideoCodecId::H264: return "H264";
        case VideoCodecId::VP8: return "VP8";
        case VideoCodecId::VP9: return "VP9";
        default: return "UNKNOWN";
    }
}

AudioCodecId CodecFactory::getAudioCodecId(uint8_t payload_type) {
    switch (payload_type) {
        case 0: return AudioCodecId::PCMU;
        case 8: return AudioCodecId::PCMA;
        case 9: return AudioCodecId::G722;
        case 18: return AudioCodecId::G729;
        case 96: return AudioCodecId::OPUS; // Dynamic
        default: return AudioCodecId::UNKNOWN;
    }
}

VideoCodecId CodecFactory::getVideoCodecId(uint8_t payload_type) {
    switch (payload_type) {
        case 96: return VideoCodecId::H264; // Dynamic
        case 97: return VideoCodecId::VP8;  // Dynamic
        case 98: return VideoCodecId::VP9;  // Dynamic
        default: return VideoCodecId::UNKNOWN;
    }
}

std::vector<AudioCodecId> CodecFactory::getSupportedAudioCodecs() {
    return {AudioCodecId::PCMU, AudioCodecId::PCMA};
}

std::vector<VideoCodecId> CodecFactory::getSupportedVideoCodecs() {
    return {VideoCodecId::H264};
}

// CodecManager implementation
CodecManager::CodecManager() {
}

CodecManager::~CodecManager() {
}

bool CodecManager::setAudioEncoder(AudioCodecId codec_id, const CodecParameters& params) {
    audio_encoder_ = CodecFactory::createAudioEncoder(codec_id);
    if (!audio_encoder_) {
        return false;
    }

    if (!audio_encoder_->configure(params)) {
        audio_encoder_.reset();
        return false;
    }

    core::Logger::info("Audio encoder set: {}", audio_encoder_->getName());
    return true;
}

bool CodecManager::setAudioDecoder(AudioCodecId codec_id, const CodecParameters& params) {
    audio_decoder_ = CodecFactory::createAudioDecoder(codec_id);
    if (!audio_decoder_) {
        return false;
    }

    if (!audio_decoder_->configure(params)) {
        audio_decoder_.reset();
        return false;
    }

    core::Logger::info("Audio decoder set: {}", audio_decoder_->getName());
    return true;
}

bool CodecManager::setVideoEncoder(VideoCodecId codec_id, const CodecParameters& params) {
    video_encoder_ = CodecFactory::createVideoEncoder(codec_id);
    if (!video_encoder_) {
        return false;
    }

    if (!video_encoder_->configure(params)) {
        video_encoder_.reset();
        return false;
    }

    core::Logger::info("Video encoder set: {}", video_encoder_->getName());
    return true;
}

bool CodecManager::setVideoDecoder(VideoCodecId codec_id, const CodecParameters& params) {
    video_decoder_ = CodecFactory::createVideoDecoder(codec_id);
    if (!video_decoder_) {
        return false;
    }

    if (!video_decoder_->configure(params)) {
        video_decoder_.reset();
        return false;
    }

    core::Logger::info("Video decoder set: {}", video_decoder_->getName());
    return true;
}

std::vector<uint8_t> CodecManager::encodeAudio(const AudioFrame& frame) {
    if (!audio_encoder_) {
        stats_.encoding_errors++;
        return {};
    }

    try {
        auto result = audio_encoder_->encode(frame);
        stats_.audio_frames_encoded++;
        return result;
    } catch (const std::exception& e) {
        core::Logger::error("Audio encoding error: {}", e.what());
        stats_.encoding_errors++;
        return {};
    }
}

AudioFrame CodecManager::decodeAudio(const std::vector<uint8_t>& data) {
    if (!audio_decoder_) {
        stats_.decoding_errors++;
        return AudioFrame({}, 0, 0);
    }

    try {
        auto result = audio_decoder_->decode(data);
        stats_.audio_frames_decoded++;
        return result;
    } catch (const std::exception& e) {
        core::Logger::error("Audio decoding error: {}", e.what());
        stats_.decoding_errors++;
        return AudioFrame({}, 0, 0);
    }
}

std::vector<uint8_t> CodecManager::encodeVideo(const VideoFrame& frame, bool keyframe) {
    if (!video_encoder_) {
        stats_.encoding_errors++;
        return {};
    }

    try {
        auto result = video_encoder_->encode(frame, keyframe);
        stats_.video_frames_encoded++;
        return result;
    } catch (const std::exception& e) {
        core::Logger::error("Video encoding error: {}", e.what());
        stats_.encoding_errors++;
        return {};
    }
}

VideoFrame CodecManager::decodeVideo(const std::vector<uint8_t>& data) {
    if (!video_decoder_) {
        stats_.decoding_errors++;
        return VideoFrame({}, 0, 0);
    }

    try {
        auto result = video_decoder_->decode(data);
        stats_.video_frames_decoded++;
        return result;
    } catch (const std::exception& e) {
        core::Logger::error("Video decoding error: {}", e.what());
        stats_.decoding_errors++;
        return VideoFrame({}, 0, 0);
    }
}

void CodecManager::reset() {
    audio_encoder_.reset();
    audio_decoder_.reset();
    video_encoder_.reset();
    video_decoder_.reset();
    stats_ = {};
}

bool CodecManager::isConfigured() const {
    return (audio_encoder_ && audio_encoder_->isConfigured()) ||
           (audio_decoder_ && audio_decoder_->isConfigured()) ||
           (video_encoder_ && video_encoder_->isConfigured()) ||
           (video_decoder_ && video_decoder_->isConfigured());
}

// Utility functions
std::string codecParametersToSdp(const CodecParameters& params, uint8_t payload_type) {
    std::string rtpmap = std::to_string(payload_type);

    if (payload_type == 0) {
        rtpmap += " PCMU/8000";
    } else if (payload_type == 8) {
        rtpmap += " PCMA/8000";
    } else if (payload_type == 96) {
        if (params.sample_rate > 0) {
            rtpmap += " H264/90000"; // Assume H.264 for PT 96
        } else {
            rtpmap += " OPUS/48000/2"; // Assume Opus for audio PT 96
        }
    }

    return rtpmap;
}

CodecParameters codecParametersFromSdp(const std::string& rtpmap, const std::string& fmtp) {
    CodecParameters params;

    // Parse rtpmap: "96 H264/90000" or "0 PCMU/8000"
    size_t space_pos = rtpmap.find(' ');
    if (space_pos != std::string::npos) {
        std::string codec_info = rtpmap.substr(space_pos + 1);
        size_t slash_pos = codec_info.find('/');

        if (slash_pos != std::string::npos) {
            std::string codec_name = codec_info.substr(0, slash_pos);
            std::string rate_info = codec_info.substr(slash_pos + 1);

            size_t second_slash = rate_info.find('/');
            if (second_slash != std::string::npos) {
                params.sample_rate = std::stoul(rate_info.substr(0, second_slash));
                params.channels = std::stoul(rate_info.substr(second_slash + 1));
            } else {
                params.sample_rate = std::stoul(rate_info);
                params.channels = 1; // Default
            }
        }
    }

    // Parse fmtp if provided
    if (!fmtp.empty()) {
        // Simple parsing for profile-level-id etc.
        if (fmtp.find("profile-level-id") != std::string::npos) {
            size_t start = fmtp.find("profile-level-id=");
            if (start != std::string::npos) {
                start += 17; // Length of "profile-level-id="
                size_t end = fmtp.find(';', start);
                if (end == std::string::npos) end = fmtp.length();
                params.profile = fmtp.substr(start, end - start);
            }
        }
    }

    return params;
}

} // namespace fmus::media
