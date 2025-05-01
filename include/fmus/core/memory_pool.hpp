#pragma once

#include <cstddef>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <cassert>
#include <new>

#include <fmus/core/error.hpp>

namespace fmus::core {

// Memory block untuk tracking free blocks
struct MemoryBlock {
    MemoryBlock* next;
};

// Memory pool untuk alokasi fixed-size blocks
class MemoryPool {
public:
    // Constructor dengan ukuran block dan jumlah block
    explicit MemoryPool(std::size_t block_size, std::size_t initial_blocks = 16)
        : block_size_(std::max(block_size, sizeof(MemoryBlock)))
        , blocks_per_chunk_(initial_blocks) {
        // Alokasi chunk pertama
        allocateChunk();
    }

    // Mencegah copy dan move
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    // Destructor
    ~MemoryPool() {
        // Bebaskan semua chunks
        for (auto* chunk : chunks_) {
            std::free(chunk);
        }
    }

    // Alokasi memory block
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Jika tidak ada free block, alokasi chunk baru
        if (!free_list_) {
            allocateChunk();
            if (!free_list_) {
                throw_error(ErrorCode::OutOfMemory, "Failed to allocate memory chunk");
            }
        }

        // Ambil block dari free list
        void* block = free_list_;
        free_list_ = free_list_->next;
        allocated_blocks_++;

        return block;
    }

    // Dealokasi memory block
    void deallocate(void* ptr) noexcept {
        if (!ptr) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Validasi pointer
        if (!isValidPointer(ptr)) {
            logger().error("Invalid pointer passed to deallocate");
            return;
        }

        // Tambahkan block ke free list
        auto* block = static_cast<MemoryBlock*>(ptr);
        block->next = free_list_;
        free_list_ = block;
        allocated_blocks_--;
    }

    // Statistik penggunaan memory
    std::size_t blockSize() const noexcept { return block_size_; }
    std::size_t allocatedBlocks() const noexcept { return allocated_blocks_.load(); }
    std::size_t totalBlocks() const noexcept { return chunks_.size() * blocks_per_chunk_; }

    // Reset pool (dealokasi semua blocks)
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Reset free list
        free_list_ = nullptr;
        allocated_blocks_ = 0;

        // Bebaskan semua chunks kecuali yang pertama
        while (chunks_.size() > 1) {
            std::free(chunks_.back());
            chunks_.pop_back();
        }

        // Reset chunk pertama
        if (!chunks_.empty()) {
            initializeChunk(chunks_[0]);
        }
    }

private:
    // Alokasi chunk baru
    void allocateChunk() {
        // Alokasi memory untuk chunk
        void* chunk = std::malloc(block_size_ * blocks_per_chunk_);
        if (!chunk) {
            throw_error(ErrorCode::OutOfMemory, "Failed to allocate memory chunk");
        }

        // Inisialisasi blocks dalam chunk
        initializeChunk(chunk);

        // Simpan chunk
        chunks_.push_back(chunk);
    }

    // Inisialisasi blocks dalam chunk
    void initializeChunk(void* chunk) {
        auto* blocks = static_cast<std::byte*>(chunk);

        // Inisialisasi setiap block
        for (std::size_t i = 0; i < blocks_per_chunk_; ++i) {
            auto* block = reinterpret_cast<MemoryBlock*>(blocks + i * block_size_);
            block->next = free_list_;
            free_list_ = block;
        }
    }

    // Validasi pointer
    bool isValidPointer(void* ptr) const noexcept {
        // Cek apakah pointer berada dalam salah satu chunk
        const auto* byte_ptr = static_cast<const std::byte*>(ptr);

        for (const auto* chunk : chunks_) {
            const auto* chunk_start = static_cast<const std::byte*>(chunk);
            const auto* chunk_end = chunk_start + (block_size_ * blocks_per_chunk_);

            if (byte_ptr >= chunk_start && byte_ptr < chunk_end) {
                // Cek alignment
                return (byte_ptr - chunk_start) % block_size_ == 0;
            }
        }

        return false;
    }

    const std::size_t block_size_;      // Ukuran setiap block
    const std::size_t blocks_per_chunk_; // Jumlah blocks per chunk

    MemoryBlock* free_list_ = nullptr;  // Linked list dari free blocks
    std::vector<void*> chunks_;         // Array dari allocated chunks
    std::atomic<std::size_t> allocated_blocks_{0}; // Jumlah blocks yang dialokasi

    mutable std::mutex mutex_;  // Mutex untuk thread safety
};

// Smart pointer untuk memory pool allocation
template<typename T>
class PoolPtr {
public:
    // Constructors
    PoolPtr() noexcept = default;

    explicit PoolPtr(MemoryPool& pool)
        : pool_(&pool)
        , ptr_(static_cast<T*>(pool.allocate())) {
        // Construct object
        new(ptr_) T();
    }

    template<typename... Args>
    PoolPtr(MemoryPool& pool, Args&&... args)
        : pool_(&pool)
        , ptr_(static_cast<T*>(pool.allocate())) {
        // Construct object dengan arguments
        new(ptr_) T(std::forward<Args>(args)...);
    }

    // Destructor
    ~PoolPtr() {
        reset();
    }

    // Copy operations
    PoolPtr(const PoolPtr&) = delete;
    PoolPtr& operator=(const PoolPtr&) = delete;

    // Move operations
    PoolPtr(PoolPtr&& other) noexcept
        : pool_(other.pool_)
        , ptr_(other.ptr_) {
        other.pool_ = nullptr;
        other.ptr_ = nullptr;
    }

    PoolPtr& operator=(PoolPtr&& other) noexcept {
        if (this != &other) {
            reset();
            pool_ = other.pool_;
            ptr_ = other.ptr_;
            other.pool_ = nullptr;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    // Reset pointer
    void reset() noexcept {
        if (ptr_) {
            ptr_->~T();  // Call destructor
            pool_->deallocate(ptr_);
            ptr_ = nullptr;
        }
        pool_ = nullptr;
    }

    // Access operators
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

    // Getters
    T* get() const noexcept { return ptr_; }
    MemoryPool* pool() const noexcept { return pool_; }

    // Boolean conversion
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    MemoryPool* pool_ = nullptr;
    T* ptr_ = nullptr;
};

// Helper function untuk membuat PoolPtr
template<typename T, typename... Args>
PoolPtr<T> make_pool_ptr(MemoryPool& pool, Args&&... args) {
    return PoolPtr<T>(pool, std::forward<Args>(args)...);
}

} // namespace fmus::core