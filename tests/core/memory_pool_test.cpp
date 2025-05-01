#include <gtest/gtest.h>
#include <fmus/core/memory_pool.hpp>
#include <thread>
#include <vector>
#include <random>
#include <atomic>

namespace fmus::core::test {

class MemoryPoolTest : public ::testing::Test {
protected:
    static constexpr std::size_t kBlockSize = 64;
    static constexpr std::size_t kInitialBlocks = 16;

    void SetUp() override {
        pool_ = std::make_unique<MemoryPool>(kBlockSize, kInitialBlocks);
    }

    void TearDown() override {
        pool_.reset();
    }

    std::unique_ptr<MemoryPool> pool_;
};

// Test alokasi dan dealokasi dasar
TEST_F(MemoryPoolTest, BasicAllocation) {
    // Alokasi single block
    void* ptr = pool_->allocate();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(pool_->allocatedBlocks(), 1);

    // Dealokasi
    pool_->deallocate(ptr);
    EXPECT_EQ(pool_->allocatedBlocks(), 0);
}

// Test multiple allocations
TEST_F(MemoryPoolTest, MultipleAllocations) {
    std::vector<void*> ptrs;

    // Alokasi semua blocks dalam chunk pertama
    for (std::size_t i = 0; i < kInitialBlocks; ++i) {
        void* ptr = pool_->allocate();
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }

    EXPECT_EQ(pool_->allocatedBlocks(), kInitialBlocks);

    // Alokasi satu lagi untuk memaksa chunk baru
    void* extra_ptr = pool_->allocate();
    ASSERT_NE(extra_ptr, nullptr);
    EXPECT_EQ(pool_->allocatedBlocks(), kInitialBlocks + 1);

    // Dealokasi semua
    for (auto ptr : ptrs) {
        pool_->deallocate(ptr);
    }
    pool_->deallocate(extra_ptr);

    EXPECT_EQ(pool_->allocatedBlocks(), 0);
}

// Test alignment
TEST_F(MemoryPoolTest, Alignment) {
    void* ptr = pool_->allocate();
    ASSERT_NE(ptr, nullptr);

    // Cek alignment
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % alignof(std::max_align_t), 0);

    pool_->deallocate(ptr);
}

// Test invalid deallocations
TEST_F(MemoryPoolTest, InvalidDeallocations) {
    // Dealokasi nullptr
    pool_->deallocate(nullptr);

    // Dealokasi invalid pointer
    int dummy;
    pool_->deallocate(&dummy);

    // Double deallocation
    void* ptr = pool_->allocate();
    pool_->deallocate(ptr);
    pool_->deallocate(ptr);  // Tidak boleh crash
}

// Test thread safety
TEST_F(MemoryPoolTest, ThreadSafety) {
    static constexpr int kNumThreads = 4;
    static constexpr int kOpsPerThread = 1000;

    std::atomic<int> total_ops{0};
    std::vector<std::thread> threads;

    // Start worker threads
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([this, &total_ops]() {
            std::vector<void*> ptrs;
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dis(0.0, 1.0);

            for (int j = 0; j < kOpsPerThread; ++j) {
                if (dis(gen) < 0.6) {  // 60% chance to allocate
                    void* ptr = pool_->allocate();
                    ASSERT_NE(ptr, nullptr);
                    ptrs.push_back(ptr);
                }
                else if (!ptrs.empty()) {  // 40% chance to deallocate
                    size_t index = gen() % ptrs.size();
                    pool_->deallocate(ptrs[index]);
                    ptrs[index] = ptrs.back();
                    ptrs.pop_back();
                }
                total_ops++;
            }

            // Cleanup remaining allocations
            for (auto ptr : ptrs) {
                pool_->deallocate(ptr);
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all operations completed
    EXPECT_EQ(total_ops, kNumThreads * kOpsPerThread);

    // Verify no memory leaks
    EXPECT_EQ(pool_->allocatedBlocks(), 0);
}

// Test PoolPtr
TEST_F(MemoryPoolTest, PoolPtr) {
    struct TestStruct {
        int value;
        std::string str;

        TestStruct() : value(0), str("default") {}
        TestStruct(int v, std::string s) : value(v), str(std::move(s)) {}
    };

    // Test default construction
    PoolPtr<TestStruct> ptr1(*pool_);
    ASSERT_TRUE(ptr1);
    EXPECT_EQ(ptr1->value, 0);
    EXPECT_EQ(ptr1->str, "default");

    // Test construction with arguments
    auto ptr2 = make_pool_ptr<TestStruct>(*pool_, 42, "test");
    ASSERT_TRUE(ptr2);
    EXPECT_EQ(ptr2->value, 42);
    EXPECT_EQ(ptr2->str, "test");

    // Test move construction
    PoolPtr<TestStruct> ptr3 = std::move(ptr2);
    ASSERT_TRUE(ptr3);
    ASSERT_FALSE(ptr2);
    EXPECT_EQ(ptr3->value, 42);

    // Test reset
    ptr3.reset();
    ASSERT_FALSE(ptr3);
}

// Test stress dengan reuse
TEST_F(MemoryPoolTest, StressTest) {
    static constexpr int kNumOperations = 10000;

    std::vector<void*> ptrs;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    for (int i = 0; i < kNumOperations; ++i) {
        if (dis(gen) < 0.7) {  // 70% chance to allocate
            void* ptr = pool_->allocate();
            ASSERT_NE(ptr, nullptr);
            ptrs.push_back(ptr);
        }
        else if (!ptrs.empty()) {  // 30% chance to deallocate
            size_t index = gen() % ptrs.size();
            pool_->deallocate(ptrs[index]);
            ptrs[index] = ptrs.back();
            ptrs.pop_back();
        }
    }

    // Cleanup
    for (auto ptr : ptrs) {
        pool_->deallocate(ptr);
    }

    EXPECT_EQ(pool_->allocatedBlocks(), 0);
}

// Test reset functionality
TEST_F(MemoryPoolTest, Reset) {
    std::vector<void*> ptrs;

    // Alokasi beberapa blocks
    for (int i = 0; i < 100; ++i) {
        ptrs.push_back(pool_->allocate());
    }

    EXPECT_GT(pool_->allocatedBlocks(), 0);

    // Reset pool
    pool_->reset();

    EXPECT_EQ(pool_->allocatedBlocks(), 0);

    // Verify pool masih bisa digunakan
    void* ptr = pool_->allocate();
    ASSERT_NE(ptr, nullptr);
    pool_->deallocate(ptr);
}

} // namespace fmus::core::test