#include <gtest/gtest.h>
#include "../headers/slab_allocator.hpp"
#include <thread>
#include <vector>

// Test basic allocation and deallocation
TEST(SlabAllocatorTest, BasicAllocFree) {
    SlabAllocator alloc(16, 128);
    
    void* ptr1 = alloc.alloc(16);
    void* ptr2 = alloc.alloc(16);
    
    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr1, ptr2); // Should be different addresses

    // Check alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 16, 0);

    alloc.free(ptr1, 16);
    alloc.free(ptr2, 16);
}

// Test multi-threaded safety (Contention test)
TEST(SlabAllocatorTest, MultiThreadedAlloc) {
    SlabAllocator alloc(16, 64);
    const int num_threads = 8;
    const int allocs_per_thread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&alloc]() {
            std::vector<void*> ptrs;
            for (int i = 0; i < allocs_per_thread; ++i) {
                ptrs.push_back(alloc.alloc(32));
            }
            for (void* p : ptrs) {
                alloc.free(p, 32);
            }
        });
    }

    for (auto& th : threads) th.join();
}
