#include <fmus/core/buffer.hpp>
#include <fmus/core/logger.hpp>

namespace fmus::core {

namespace {

// Helper untuk validasi alignment
bool validateAlignment(const void* ptr) noexcept {
    return reinterpret_cast<std::uintptr_t>(ptr) % alignof(std::max_align_t) == 0;
}

// Helper untuk optimized copy
void optimizedCopy(std::byte* dst, const std::byte* src, std::size_t size) noexcept {
    // Gunakan memcpy untuk aligned data
    if (validateAlignment(dst) && validateAlignment(src)) {
        std::memcpy(dst, src, size);
        return;
    }

    // Fallback ke byte-by-byte copy untuk unaligned data
    for (std::size_t i = 0; i < size; ++i) {
        dst[i] = src[i];
    }
}

// Helper untuk optimized move
void optimizedMove(std::byte* dst, const std::byte* src, std::size_t size) noexcept {
    // Gunakan memmove untuk aligned data
    if (validateAlignment(dst) && validateAlignment(src)) {
        std::memmove(dst, src, size);
        return;
    }

    // Fallback ke byte-by-byte move untuk unaligned data
    if (dst < src) {
        // Copy forward
        for (std::size_t i = 0; i < size; ++i) {
            dst[i] = src[i];
        }
    } else {
        // Copy backward
        for (std::size_t i = size; i > 0; --i) {
            dst[i-1] = src[i-1];
        }
    }
}

} // namespace

// Buffer implementation

void Buffer::append(const void* data, std::size_t size) {
    if (size == 0) return;
    if (!data) {
        logger().error("Attempt to append null data to buffer");
        throw_error(ErrorCode::InvalidArgument, "Null data pointer");
    }

    try {
        const std::size_t required_size = size_ + size;
        if (required_size > capacity_) {
            // Gunakan exponential growth untuk menghindari frequent reallocation
            const std::size_t new_capacity = std::max(
                required_size,
                capacity_ + (capacity_ >> 1)  // Grow by 1.5x
            );
            reserve(new_capacity);
        }

        optimizedCopy(data_.get() + size_,
                     static_cast<const std::byte*>(data),
                     size);
        size_ = required_size;
    }
    catch (const std::exception& e) {
        logger().error("Failed to append data to buffer: {}", e.what());
        throw;
    }
}

void Buffer::consume(std::size_t size) {
    if (size >= size_) {
        clear();
        return;
    }

    optimizedMove(data_.get(),
                 data_.get() + size,
                 size_ - size);
    size_ -= size;
}

// PoolBuffer implementation

void PoolBuffer::allocate(std::size_t size) {
    try {
        release();
        data_ = static_cast<std::byte*>(pool_->allocate());
        if (!data_) {
            throw_error(ErrorCode::OutOfMemory,
                "Failed to allocate pool buffer of size {}", size);
        }
        size_ = size;
    }
    catch (const std::exception& e) {
        logger().error("Pool buffer allocation failed: {}", e.what());
        throw;
    }
}

} // namespace fmus::core