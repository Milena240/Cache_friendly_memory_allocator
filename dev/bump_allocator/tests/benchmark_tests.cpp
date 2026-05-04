#include "../headers/allocator.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>

const int NUM_THREADS = 8;
const int ALLOCS_PER_THREAD = 100000;
const size_t ALLOC_SIZE = 32;

void 
stress_test(BumpAllocator& alloc)
{
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
        volatile void* p = alloc.alloc(ALLOC_SIZE);
        (void)p;
    }
}

void test_out_of_memory()
{
    BumpAllocator alloc(64, LockType::NONE);
    void* p1 = alloc.alloc(32);
    void* p2 = alloc.alloc(40); 
    assert(p1 != nullptr);
    assert(p2 == nullptr);
    std::cout << "[PASS] Out-of-memory test\n";
}

void test_reset()
{
    BumpAllocator alloc(128, LockType::NONE);
    alloc.alloc(64);
    alloc.reset();
    void* p = alloc.alloc(128);
    assert(p != nullptr);
    std::cout << "[PASS] Reset works\n";
}


double
run_test(LockType type) 
{
    BumpAllocator alloc(100 * 1024 * 1024, type);
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(stress_test, std::ref(alloc));
    }
    for (auto& t : threads) t.join();
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> elapsed = end - start;
    return elapsed.count();
}

int
main() 
{
    std::cout << "Starting Benchmarks (" << NUM_THREADS << " threads)..." << std::endl;

    double mutex_time = run_test(LockType::MUTEX);
    std::cout << "Mutex Time:    " << mutex_time << " ms" << std::endl;

    double spin_time = run_test(LockType::SPIN_LOCK);
    std::cout << "Spinlock Time: " << spin_time << " ms" << std::endl;

    std::cout << "Winner: " << (spin_time < mutex_time ? "SPINLOCK" : "MUTEX") << std::endl;
    std::cout << "Performance Gap: " << (mutex_time / spin_time) << "x" << std::endl;

    return 0;
}
