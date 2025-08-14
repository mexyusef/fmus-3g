#include "fmus/media/frame.hpp"

namespace fmus::media {

AudioFrame::AudioFrame(const std::vector<uint8_t>& data, int sample_rate, int channels)
    : data_(data), sample_rate_(sample_rate), channels_(channels) {
}

VideoFrame::VideoFrame(const std::vector<uint8_t>& data, int width, int height)
    : data_(data), width_(width), height_(height) {
}

} // namespace fmus::media
