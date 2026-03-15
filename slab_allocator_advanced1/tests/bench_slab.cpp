#include <benchmark/benchmark.h>
#include "../headers/slab_allocator.hpp"

// Single-threaded benchmark
static void BM_SlabAllocFree(benchmark::State& state) {
    SlabAllocator alloc(16, 1024);
    
    // Explicit while loop prevents the 'begin/end' compiler error
    while (state.KeepRunning()) {
        void* p = alloc.alloc(64);
        
        // Tells the compiler: "Don't optimize this pointer away"
        benchmark::DoNotOptimize(p); 
        
        alloc.free(p, 64);
    }
}
BENCHMARK(BM_SlabAllocFree);

// Multi-threaded benchmark
static void BM_MultiThreadedSlab(benchmark::State& state) {
    // Shared allocator across threads
    static SlabAllocator global_alloc(16, 1024);
    
    while (state.KeepRunning()) {
        void* p = global_alloc.alloc(64);
        benchmark::DoNotOptimize(p);
        global_alloc.free(p, 64);
    }
}
// Run on 1, 2, 4, and 8 threads to test scaling
BENCHMARK(BM_MultiThreadedSlab)->ThreadRange(1, 8);

BENCHMARK_MAIN();
