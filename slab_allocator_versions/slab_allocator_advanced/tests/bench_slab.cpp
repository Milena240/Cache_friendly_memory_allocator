#include <benchmark/benchmark.h>
#include "../headers/slab_allocator.hpp"

static void BM_SlabAllocFree(benchmark::State& state) {
    // Convert the argument back to LockType
    LockType lt = static_cast<LockType>(state.range(0));
    
    // Setup the allocator
    SlabAllocator alloc(8, 64, lt);

    // Using the classic while(state.KeepRunning()) loop
    while (state.KeepRunning()) {
        void* p = alloc.alloc(16);
        if (p) {
            alloc.free(p, 16);
        }
        // Force the compiler to actually perform the operation
        benchmark::DoNotOptimize(p);
    }
}

// Register benchmarks. 
// 0: NONE, 1: MUTEX, 2: SPIN_LOCK
BENCHMARK(BM_SlabAllocFree)->Arg(0);
BENCHMARK(BM_SlabAllocFree)->Arg(1);
BENCHMARK(BM_SlabAllocFree)->Arg(2);

BENCHMARK_MAIN();
