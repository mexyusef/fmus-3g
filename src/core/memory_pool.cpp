#include <fmus/core/memory_pool.hpp>
#include <fmus/core/logger.hpp>

namespace fmus::core {

namespace {

// Helper untuk validasi ukuran block
bool validateBlockSize(std::size_t block_size) {
    // Cek minimum size
    if (block_size < sizeof(MemoryBlock)) {
        logger().error("Block size too small: {} bytes (minimum: {} bytes)",
            block_size, sizeof(MemoryBlock));
        return false;
    }

    // Cek alignment
    if (block_size % alignof(std::max_align_t) != 0) {
        logger().error("Block size {} is not properly aligned", block_size);
        return false;
    }

    return true;
}

// Helper untuk validasi jumlah blocks
bool validateBlockCount(std::size_t block_count) {
    // Cek minimum blocks
    if (block_count == 0) {
        logger().error("Block count cannot be zero");
        return false;
    }

    // Cek maximum blocks (arbitrary limit untuk mencegah allocation errors)
    constexpr std::size_t max_blocks = 1024 * 1024;  // 1M blocks
    if (block_count > max_blocks) {
        logger().error("Block count too large: {} (maximum: {})",
            block_count, max_blocks);
        return false;
    }

    return true;
}

} // namespace

// Global memory pool registry untuk tracking memory leaks
class MemoryPoolRegistry {
public:
    static MemoryPoolRegistry& instance() {
        static MemoryPoolRegistry registry;
        return registry;
    }

    void registerPool(MemoryPool* pool) {
        std::lock_guard<std::mutex> lock(mutex_);
        pools_.push_back(pool);
    }

    void unregisterPool(MemoryPool* pool) {
        std::lock_guard<std::mutex> lock(mutex_);
        pools_.erase(std::remove(pools_.begin(), pools_.end(), pool),
                    pools_.end());
    }

    // Report memory leaks pada program exit
    ~MemoryPoolRegistry() {
        for (const auto* pool : pools_) {
            auto allocated = pool->allocatedBlocks();
            if (allocated > 0) {
                logger().warn("Memory leak detected: {} blocks still allocated",
                    allocated);
            }
        }
    }

private:
    MemoryPoolRegistry() = default;

    std::vector<MemoryPool*> pools_;
    std::mutex mutex_;
};

// Constructor dengan validasi
MemoryPool::MemoryPool(std::size_t block_size, std::size_t initial_blocks)
    : block_size_(std::max(block_size, sizeof(MemoryBlock)))
    , blocks_per_chunk_(initial_blocks) {

    // Validasi parameters
    if (!validateBlockSize(block_size_) ||
        !validateBlockCount(blocks_per_chunk_)) {
        throw_error(ErrorCode::InvalidArgument,
            "Invalid memory pool parameters");
    }

    // Alokasi chunk pertama
    try {
        allocateChunk();
    }
    catch (const std::exception& e) {
        throw_error(ErrorCode::OutOfMemory,
            "Failed to allocate initial chunk: {}", e.what());
    }

    // Register pool
    MemoryPoolRegistry::instance().registerPool(this);
}

// Destructor dengan cleanup
MemoryPool::~MemoryPool() {
    // Unregister pool
    MemoryPoolRegistry::instance().unregisterPool(this);

    // Log warning jika ada memory leak
    auto allocated = allocatedBlocks();
    if (allocated > 0) {
        logger().warn("Memory pool destroyed with {} blocks still allocated",
            allocated);
    }

    // Cleanup chunks
    for (auto* chunk : chunks_) {
        std::free(chunk);
    }
}

} // namespace fmus::core