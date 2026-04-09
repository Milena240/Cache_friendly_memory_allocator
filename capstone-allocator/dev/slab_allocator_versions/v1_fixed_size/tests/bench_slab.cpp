#include <benchmark/benchmark.h>
#include "../headers/slab_allocator.hpp"

static void BM_SlabAllocFree(benchmark::State& state) {
    LockType lt = static_cast<LockType>(state.range(0));
    SlabAllocator alloc(64, 10000, lt);

    while (state.KeepRunning()) {
        void* p = alloc.alloc();
        if (p) {
            alloc.free(p);
        }
        benchmark::DoNotOptimize(p);
    }
}

BENCHMARK(BM_SlabAllocFree)->Arg(0);
BENCHMARK(BM_SlabAllocFree)->Arg(1);
BENCHMARK(BM_SlabAllocFree)->Arg(2);

BENCHMARK_MAIN();
