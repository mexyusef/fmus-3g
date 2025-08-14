#pragma once

#include "frame.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace fmus::media {

// Codec Types
enum class CodecType {
    AUDIO,
    VIDEO
};

// Audio Codec IDs (matching RTP payload types)
enum class AudioCodecId {
    PCMU = 0,       // G.711 μ-law
    PCMA = 8,       // G.711 A-law
    G722 = 9,       // G.722
    G729 = 18,      // G.729
    OPUS = 96,      // Opus (dynamic)
    UNKNOWN = 255
};

// Video Codec IDs
enum class VideoCodecId {
    H264 = 96,      // H.264 (dynamic)
    VP8 = 97,       // VP8 (dynamic)
    VP9 = 98,       // VP9 (dynamic)
    UNKNOWN = 255
};

// Codec Parameters
struct CodecParameters {
    // Common parameters
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint32_t bitrate = 0;
    
    // Video-specific parameters
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fps = 0;
    
    // Codec-specific parameters
    std::string profile;
    std::string level;
    std::vector<std::pair<std::string, std::string>> extra_params;
    
    void setParameter(const std::string& key, const std::string& value) {
        extra_params.emplace_back(key, value);
    }
    
    std::string getParameter(const std::string& key) const {
        for (const auto& param : extra_params) {
            if (param.first == key) {
                return param.second;
            }
        }
        return "";
    }
};

// Base Codec Interface
class Codec {
public:
    virtual ~Codec() = default;
    
    // Basic properties
    virtual CodecType getType() const = 0;
    virtual std::string getName() const = 0;
    virtual uint8_t getPayloadType() const = 0;
    
    // Configuration
    virtual bool configure(const CodecParameters& params) = 0;
    virtual CodecParameters getParameters() const = 0;
    
    // Codec operations
    virtual bool isConfigured() const = 0;
    virtual void reset() = 0;
};

// Audio Encoder Interface
class AudioEncoder : public Codec {
public:
    virtual ~AudioEncoder() = default;
    
    CodecType getType() const override { return CodecType::AUDIO; }
    
    // Encoding
    virtual std::vector<uint8_t> encode(const AudioFrame& frame) = 0;
    virtual bool encode(const AudioFrame& frame, std::vector<uint8_t>& output) = 0;
    
    // Frame size information
    virtual size_t getFrameSize() const = 0; // Samples per frame
    virtual size_t getEncodedFrameSize() const = 0; // Bytes per encoded frame
};

// Audio Decoder Interface
class AudioDecoder : public Codec {
public:
    virtual ~AudioDecoder() = default;
    
    CodecType getType() const override { return CodecType::AUDIO; }
    
    // Decoding
    virtual AudioFrame decode(const std::vector<uint8_t>& data) = 0;
    virtual bool decode(const std::vector<uint8_t>& data, AudioFrame& frame) = 0;
    
    // Frame size information
    virtual size_t getFrameSize() const = 0; // Samples per frame
    virtual size_t getDecodedFrameSize() const = 0; // Bytes per decoded frame
};

// Video Encoder Interface
class VideoEncoder : public Codec {
public:
    virtual ~VideoEncoder() = default;
    
    CodecType getType() const override { return CodecType::VIDEO; }
    
    // Encoding
    virtual std::vector<uint8_t> encode(const VideoFrame& frame, bool keyframe = false) = 0;
    virtual bool encode(const VideoFrame& frame, std::vector<uint8_t>& output, bool keyframe = false) = 0;
    
    // Keyframe control
    virtual void requestKeyframe() = 0;
    virtual bool isKeyframe(const std::vector<uint8_t>& data) const = 0;
};

// Video Decoder Interface
class VideoDecoder : public Codec {
public:
    virtual ~VideoDecoder() = default;
    
    CodecType getType() const override { return CodecType::VIDEO; }
    
    // Decoding
    virtual VideoFrame decode(const std::vector<uint8_t>& data) = 0;
    virtual bool decode(const std::vector<uint8_t>& data, VideoFrame& frame) = 0;
    
    // Frame information
    virtual bool isKeyframe(const std::vector<uint8_t>& data) const = 0;
};

// Codec Factory
class CodecFactory {
public:
    // Audio codec creation
    static std::unique_ptr<AudioEncoder> createAudioEncoder(AudioCodecId codec_id);
    static std::unique_ptr<AudioDecoder> createAudioDecoder(AudioCodecId codec_id);
    
    // Video codec creation
    static std::unique_ptr<VideoEncoder> createVideoEncoder(VideoCodecId codec_id);
    static std::unique_ptr<VideoDecoder> createVideoDecoder(VideoCodecId codec_id);
    
    // Codec information
    static std::string getCodecName(AudioCodecId codec_id);
    static std::string getCodecName(VideoCodecId codec_id);
    
    static AudioCodecId getAudioCodecId(uint8_t payload_type);
    static VideoCodecId getVideoCodecId(uint8_t payload_type);
    
    static std::vector<AudioCodecId> getSupportedAudioCodecs();
    static std::vector<VideoCodecId> getSupportedVideoCodecs();
};

// Codec Manager - manages multiple codecs for a session
class CodecManager {
public:
    CodecManager();
    ~CodecManager();
    
    // Audio codec management
    bool setAudioEncoder(AudioCodecId codec_id, const CodecParameters& params);
    bool setAudioDecoder(AudioCodecId codec_id, const CodecParameters& params);
    
    AudioEncoder* getAudioEncoder() const { return audio_encoder_.get(); }
    AudioDecoder* getAudioDecoder() const { return audio_decoder_.get(); }
    
    // Video codec management
    bool setVideoEncoder(VideoCodecId codec_id, const CodecParameters& params);
    bool setVideoDecoder(VideoCodecId codec_id, const CodecParameters& params);
    
    VideoEncoder* getVideoEncoder() const { return video_encoder_.get(); }
    VideoDecoder* getVideoDecoder() const { return video_decoder_.get(); }
    
    // Encoding/Decoding shortcuts
    std::vector<uint8_t> encodeAudio(const AudioFrame& frame);
    AudioFrame decodeAudio(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> encodeVideo(const VideoFrame& frame, bool keyframe = false);
    VideoFrame decodeVideo(const std::vector<uint8_t>& data);
    
    // Configuration
    void reset();
    bool isConfigured() const;
    
    // Statistics
    struct Stats {
        uint64_t audio_frames_encoded = 0;
        uint64_t audio_frames_decoded = 0;
        uint64_t video_frames_encoded = 0;
        uint64_t video_frames_decoded = 0;
        uint64_t encoding_errors = 0;
        uint64_t decoding_errors = 0;
    };
    
    Stats getStats() const { return stats_; }
    void resetStats() { stats_ = {}; }

private:
    std::unique_ptr<AudioEncoder> audio_encoder_;
    std::unique_ptr<AudioDecoder> audio_decoder_;
    std::unique_ptr<VideoEncoder> video_encoder_;
    std::unique_ptr<VideoDecoder> video_decoder_;
    
    Stats stats_;
};

// Utility functions
std::string codecParametersToSdp(const CodecParameters& params, uint8_t payload_type);
CodecParameters codecParametersFromSdp(const std::string& rtpmap, const std::string& fmtp = "");

} // namespace fmus::media
