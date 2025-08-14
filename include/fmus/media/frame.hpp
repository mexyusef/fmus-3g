#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace fmus::media {

enum class MediaType {
    Audio,
    Video
};

class AudioFrame {
public:
    AudioFrame() = default;
    AudioFrame(const std::vector<uint8_t>& data, int sample_rate, int channels);
    
    const std::vector<uint8_t>& getData() const { return data_; }
    std::vector<uint8_t>& getData() { return data_; }
    
    int getSampleRate() const { return sample_rate_; }
    int getChannels() const { return channels_; }
    size_t getSize() const { return data_.size(); }
    
    void setData(const std::vector<uint8_t>& data) { data_ = data; }
    void setSampleRate(int rate) { sample_rate_ = rate; }
    void setChannels(int channels) { channels_ = channels; }

private:
    std::vector<uint8_t> data_;
    int sample_rate_ = 8000;
    int channels_ = 1;
};

class VideoFrame {
public:
    VideoFrame() = default;
    VideoFrame(const std::vector<uint8_t>& data, int width, int height);
    
    const std::vector<uint8_t>& getData() const { return data_; }
    std::vector<uint8_t>& getData() { return data_; }
    
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    size_t getSize() const { return data_.size(); }
    
    void setData(const std::vector<uint8_t>& data) { data_ = data; }
    void setDimensions(int width, int height) { width_ = width; height_ = height; }

private:
    std::vector<uint8_t> data_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace fmus::media
