#include <benchmark/benchmark.h>
#include "../headers/slab_allocator.hpp"

static void BM_SlabAllocFree(benchmark::State& state) {
    LockType lt = static_cast<LockType>(state.range(0));
    SlabAllocator alloc(8, 64, lt);

    while (state.KeepRunning()) {
        void* p = alloc.alloc(16);
        if (p) {
            alloc.free(p, 16);
        }
        benchmark::DoNotOptimize(p);
    }
}

BENCHMARK(BM_SlabAllocFree)->Arg(0);
BENCHMARK(BM_SlabAllocFree)->Arg(1);
BENCHMARK(BM_SlabAllocFree)->Arg(2);

BENCHMARK_MAIN();
