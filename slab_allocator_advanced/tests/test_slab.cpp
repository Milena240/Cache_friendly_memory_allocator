#include <gtest/gtest.h>
#include "../headers/slab_allocator.hpp"
#include <vector>
#include <thread>

// Test basic allocation and deallocation
TEST(SlabAllocatorTest, BasicAllocFree)
{
    SlabAllocator alloc(8, 64, LockType::NONE);
    void* ptr = alloc.alloc(16);
    EXPECT_NE(ptr, nullptr);
    alloc.free(ptr, 16);
}

// Test memory exhaustion
TEST(SlabAllocatorTest, CapacityExhaustion)
{
    size_t count = 1000;
    SlabAllocator alloc(32, 32, LockType::NONE);
    
    std::vector<void*> ptrs;
    for(size_t i = 0; i < count; ++i) {
        ptrs.push_back(alloc.alloc(32));
        EXPECT_NE(ptrs.back(), nullptr);
    }
    
    // Should be null now
    EXPECT_EQ(alloc.alloc(32), nullptr);

    for(void* p : ptrs) alloc.free(p, 32);
    
    // Should be able to alloc again after free
    EXPECT_NE(alloc.alloc(32), nullptr);
}

// Test multi-threaded safety with SpinLock
TEST(SlabAllocatorTest, ThreadSafetySpinLock)
{
    SlabAllocator alloc(16, 64, LockType::SPIN_LOCK);
    auto worker = [&]() {
        for(int i = 0; i < 100; ++i) {
            void* p = alloc.alloc(16);
            if(p) {
                std::this_thread::yield();
                alloc.free(p, 16);
            }
        }
    };

    std::vector<std::thread> threads;
    for(int i = 0; i < 10; ++i) threads.emplace_back(worker);
    for(auto& t : threads) t.join();
}

