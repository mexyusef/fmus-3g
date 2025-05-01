#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <string_view>
#include <concepts>
#include <type_traits>
#include <algorithm>
#include <cassert>

#include <fmus/core/memory_pool.hpp>
#include <fmus/core/error.hpp>

namespace fmus::core {

// Concept untuk tipe yang bisa digunakan sebagai buffer data
template<typename T>
concept BufferCompatible = std::is_trivially_copyable_v<T>;

// Buffer view yang tidak memiliki data
template<BufferCompatible T>
class BufferView {
public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;

    // Constructors
    constexpr BufferView() noexcept = default;

    constexpr BufferView(pointer data, size_type size) noexcept
        : data_(data), size_(size) {}

    constexpr BufferView(const BufferView&) noexcept = default;
    constexpr BufferView& operator=(const BufferView&) noexcept = default;

    // Dari std::span
    template<std::size_t N>
    constexpr BufferView(std::span<T, N> span) noexcept
        : data_(span.data()), size_(span.size()) {}

    // Dari array
    template<std::size_t N>
    constexpr BufferView(T (&arr)[N]) noexcept
        : data_(arr), size_(N) {}

    // Dari vector
    constexpr BufferView(std::vector<T>& vec) noexcept
        : data_(vec.data()), size_(vec.size()) {}

    // Iterators
    constexpr iterator begin() noexcept { return data_; }
    constexpr const_iterator begin() const noexcept { return data_; }
    constexpr iterator end() noexcept { return data_ + size_; }
    constexpr const_iterator end() const noexcept { return data_ + size_; }

    // Element access
    constexpr reference operator[](size_type idx) noexcept { return data_[idx]; }
    constexpr const_reference operator[](size_type idx) const noexcept { return data_[idx]; }

    constexpr reference at(size_type idx) {
        if (idx >= size_) throw_error(ErrorCode::InvalidArgument, "Buffer index out of range");
        return data_[idx];
    }

    constexpr const_reference at(size_type idx) const {
        if (idx >= size_) throw_error(ErrorCode::InvalidArgument, "Buffer index out of range");
        return data_[idx];
    }

    // Capacity
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }

    // Data access
    constexpr pointer data() noexcept { return data_; }
    constexpr const_pointer data() const noexcept { return data_; }

    // Subview
    constexpr BufferView subview(size_type offset, size_type count) const {
        if (offset > size_) throw_error(ErrorCode::InvalidArgument, "Invalid offset");
        if (count > size_ - offset) throw_error(ErrorCode::InvalidArgument, "Invalid count");
        return BufferView(data_ + offset, count);
    }

    // Konversi ke std::span
    constexpr operator std::span<T>() const noexcept {
        return std::span<T>(data_, size_);
    }

private:
    pointer data_ = nullptr;
    size_type size_ = 0;
};

// Buffer yang memiliki data sendiri
template<BufferCompatible T>
class Buffer {
public:
    using value_type = T;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;

    // Constructors
    Buffer() = default;

    explicit Buffer(size_type size)
        : data_(size) {}

    Buffer(const T* data, size_type size)
        : data_(data, data + size) {}

    Buffer(const Buffer&) = default;
    Buffer(Buffer&&) noexcept = default;
    Buffer& operator=(const Buffer&) = default;
    Buffer& operator=(Buffer&&) noexcept = default;

    // Dari BufferView
    explicit Buffer(const BufferView<T>& view)
        : data_(view.begin(), view.end()) {}

    // Iterators
    iterator begin() noexcept { return data_.begin(); }
    const_iterator begin() const noexcept { return data_.begin(); }
    iterator end() noexcept { return data_.end(); }
    const_iterator end() const noexcept { return data_.end(); }

    // Element access
    reference operator[](size_type idx) noexcept { return data_[idx]; }
    const_reference operator[](size_type idx) const noexcept { return data_[idx]; }

    reference at(size_type idx) { return data_.at(idx); }
    const_reference at(size_type idx) const { return data_.at(idx); }

    // Capacity
    [[nodiscard]] size_type size() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
    [[nodiscard]] size_type capacity() const noexcept { return data_.capacity(); }
    [[nodiscard]] size_type size_bytes() const noexcept { return size() * sizeof(T); }

    void reserve(size_type new_cap) { data_.reserve(new_cap); }
    void resize(size_type count) { data_.resize(count); }
    void clear() noexcept { data_.clear(); }

    // Data access
    pointer data() noexcept { return data_.data(); }
    const_pointer data() const noexcept { return data_.data(); }

    // Modifiers
    void append(const T* data, size_type size) {
        data_.insert(data_.end(), data, data + size);
    }

    void append(const BufferView<T>& view) {
        data_.insert(data_.end(), view.begin(), view.end());
    }

    void append(const Buffer& other) {
        data_.insert(data_.end(), other.begin(), other.end());
    }

    // View conversion
    operator BufferView<T>() const noexcept {
        return BufferView<T>(data_.data(), data_.size());
    }

    BufferView<T> view() const noexcept {
        return BufferView<T>(data_.data(), data_.size());
    }

    // Subview
    BufferView<T> subview(size_type offset, size_type count) const {
        if (offset > size()) throw_error(ErrorCode::InvalidArgument, "Invalid offset");
        if (count > size() - offset) throw_error(ErrorCode::InvalidArgument, "Invalid count");
        return BufferView<T>(data_.data() + offset, count);
    }

private:
    std::vector<T> data_;
};

// Helper type aliases
using ByteBufferView = BufferView<std::uint8_t>;
using ByteBuffer = Buffer<std::uint8_t>;

// Helper functions untuk string conversion
inline ByteBufferView as_bytes(std::string_view str) noexcept {
    return ByteBufferView(
        reinterpret_cast<const std::uint8_t*>(str.data()),
        str.size()
    );
}

inline std::string_view as_string(ByteBufferView buffer) noexcept {
    return std::string_view(
        reinterpret_cast<const char*>(buffer.data()),
        buffer.size()
    );
}

// Buffer untuk manajemen data dengan efficient memory handling
class Buffer {
public:
    // Constructors
    Buffer() = default;

    explicit Buffer(std::size_t capacity)
        : data_(std::make_unique<std::byte[]>(capacity))
        , capacity_(capacity) {}

    // Constructor dari existing data
    Buffer(const void* data, std::size_t size)
        : Buffer(size) {
        append(data, size);
    }

    // Copy constructor - deep copy
    Buffer(const Buffer& other)
        : Buffer(other.data(), other.size()) {}

    // Move constructor
    Buffer(Buffer&&) noexcept = default;

    // Assignment operators
    Buffer& operator=(const Buffer& other) {
        if (this != &other) {
            Buffer temp(other);
            swap(temp);
        }
        return *this;
    }

    Buffer& operator=(Buffer&&) noexcept = default;

    // Destructor
    ~Buffer() = default;

    // Swap operation
    void swap(Buffer& other) noexcept {
        data_.swap(other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    // Reserve capacity
    void reserve(std::size_t new_capacity) {
        if (new_capacity <= capacity_) return;

        auto new_data = std::make_unique<std::byte[]>(new_capacity);
        if (size_ > 0) {
            std::memcpy(new_data.get(), data_.get(), size_);
        }

        data_ = std::move(new_data);
        capacity_ = new_capacity;
    }

    // Resize buffer
    void resize(std::size_t new_size) {
        if (new_size > capacity_) {
            reserve(std::max(new_size, capacity_ * 2));
        }
        size_ = new_size;
    }

    // Clear buffer
    void clear() noexcept {
        size_ = 0;
    }

    // Append data
    void append(const void* data, std::size_t size) {
        if (size == 0) return;
        if (!data) throw_error(ErrorCode::InvalidArgument, "Null data pointer");

        const std::size_t required_size = size_ + size;
        if (required_size > capacity_) {
            reserve(std::max(required_size, capacity_ * 2));
        }

        std::memcpy(data_.get() + size_, data, size);
        size_ = required_size;
    }

    // Append dari string_view
    void append(std::string_view str) {
        append(str.data(), str.size());
    }

    // Append dari buffer
    void append(const Buffer& other) {
        append(other.data(), other.size());
    }

    // Remove data dari depan
    void consume(std::size_t size) {
        if (size >= size_) {
            clear();
            return;
        }

        std::memmove(data_.get(), data_.get() + size, size_ - size);
        size_ -= size;
    }

    // Access data
    const std::byte* data() const noexcept { return data_.get(); }
    std::byte* data() noexcept { return data_.get(); }

    // Get size dan capacity
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return size_ == 0; }

    // Get view ke data
    std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(data(), size());
    }

    std::span<std::byte> view() noexcept {
        return std::span<std::byte>(data(), size());
    }

    // Get string view
    std::string_view string_view() const noexcept {
        return std::string_view(
            reinterpret_cast<const char*>(data()),
            size());
    }

private:
    std::unique_ptr<std::byte[]> data_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

// Pool-allocated buffer untuk efficient reuse
class PoolBuffer {
public:
    explicit PoolBuffer(MemoryPool& pool)
        : pool_(&pool) {}

    // Copy/move constructors
    PoolBuffer(const PoolBuffer&) = delete;
    PoolBuffer& operator=(const PoolBuffer&) = delete;

    PoolBuffer(PoolBuffer&& other) noexcept
        : pool_(other.pool_)
        , data_(other.data_)
        , size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    PoolBuffer& operator=(PoolBuffer&& other) noexcept {
        if (this != &other) {
            release();
            pool_ = other.pool_;
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // Destructor
    ~PoolBuffer() {
        release();
    }

    // Allocate buffer
    void allocate(std::size_t size) {
        release();
        data_ = static_cast<std::byte*>(pool_->allocate());
        size_ = size;
    }

    // Release buffer back to pool
    void release() noexcept {
        if (data_) {
            pool_->deallocate(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    // Access data
    const std::byte* data() const noexcept { return data_; }
    std::byte* data() noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    // Get views
    std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(data(), size());
    }

    std::span<std::byte> view() noexcept {
        return std::span<std::byte>(data(), size());
    }

private:
    MemoryPool* pool_;
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

// Helper untuk membuat buffer dari string
inline Buffer make_buffer(std::string_view str) {
    return Buffer(str.data(), str.size());
}

// Helper untuk membuat buffer dengan capacity
inline Buffer make_buffer(std::size_t capacity) {
    return Buffer(capacity);
}

} // namespace fmus::core