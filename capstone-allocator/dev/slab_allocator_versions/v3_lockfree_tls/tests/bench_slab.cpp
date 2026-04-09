#include <benchmark/benchmark.h>
#include "../headers/slab_allocator.hpp"

static void BM_SlabAllocFree(benchmark::State& state) {
    SlabAllocator alloc(16, 1024);

    while (state.KeepRunning()) {
        void* p = alloc.alloc(64);
        benchmark::DoNotOptimize(p);
        alloc.free(p, 64);
    }
}
BENCHMARK(BM_SlabAllocFree);

static void BM_MultiThreadedSlab(benchmark::State& state) {
    static SlabAllocator global_alloc(16, 1024);

    while (state.KeepRunning()) {
        void* p = global_alloc.alloc(64);
        benchmark::DoNotOptimize(p);
        global_alloc.free(p, 64);
    }
}
BENCHMARK(BM_MultiThreadedSlab)->ThreadRange(1, 8);

BENCHMARK_MAIN();
