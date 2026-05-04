#define _POSIX_C_SOURCE 200809L

#include "dynamic_pool.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define WARMUP_REPS   3
#define MEASURE_REPS  10

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static const int SIZES[] = { 4, 100, 10000, 100000, 500000, 1000000 };
#define N_SIZES (int)(sizeof(SIZES)/sizeof(SIZES[0]))

static void separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}
static void header(const char* t) {
    separator();
    printf("  %s\n", t);
    separator();
}

static double bench_list_traverse(DListNode* head) {
    long volatile sink = 0;
    double best = 1e18;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        sink += dlist_traverse_sum(head);
        double t1 = now_ms();
        if (r >= WARMUP_REPS && (t1-t0) < best)
            best = t1 - t0;
    }
    (void)sink;
    return best;
}

static double bench_alloc_list(DynamicListPool* p, int size) {
    double best = 1e18;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        dlist_build_pooled(p, size);
        double t1 = now_ms();
        if (r >= WARMUP_REPS && (t1 - t0) < best)
            best = t1 - t0;
    }
    return best;
}

static double bench_alloc_scattered(int size) {
    double best = 1e18;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        DListNode* h = dlist_build_scattered(size);
        double t1 = now_ms();
        if (r >= WARMUP_REPS && (t1-t0) < best)
            best = t1 - t0;
        dlist_free_scattered(h);
    }
    return best;
}

static void exp1_memory_usage(void) {
    header("EXPERIMENT 1 — Memory usage per list size");
    printf("  Fixed pool reserves 16 MB regardless of list size.\n"
           "  Dynamic pool allocates only what is needed.\n"
           "  Scattered malloc adds ~32 bytes overhead per node.\n\n");

    size_t fixed_pool_bytes = (size_t)(1 << 20) * sizeof(DListNode);

    printf("  %-10s  %12s  %12s  %16s  %10s\n",
           "Size", "Fixed pool", "Dynamic pool", "Scattered malloc", "Dyn saving");
    printf("  %-10s  %12s  %12s  %16s  %10s\n",
           "────", "──────────", "────────────", "────────────────", "──────────");

    DynamicListPool p;
    memset(&p, 0, sizeof(p));
    for (int si = 0; si < N_SIZES; si++) {
        int n = SIZES[si];

        dlist_build_pooled(&p, n);
        PoolStats ps = dynamic_list_stats(&p);

        size_t scattered_bytes = (size_t)n * (sizeof(DListNode) + 16);

        double saving_pct = 100.0 * (1.0 - (double)ps.bytes_allocated
                                         / (double)fixed_pool_bytes);

        printf("  %-10d  %9zu KB  %9zu KB  %12zu KB  %9.1f%%\n",
               n,
               fixed_pool_bytes   / 1024,
               ps.bytes_allocated / 1024,
               scattered_bytes    / 1024,
               saving_pct);

        dynamic_list_reset(&p);
    }
    printf("\n");
    printf("  Dynamic pool chunk details (CHUNK_SIZE = %d nodes):\n\n", CHUNK_SIZE);
    printf("  %-10s  %10s  %12s  %12s  %10s\n",
           "Size", "Chunks", "Alloc (KB)", "Used (KB)", "Util %%");
    printf("  %-10s  %10s  %12s  %12s  %10s\n",
           "────", "──────", "──────────", "─────────", "──────");
    for (int si = 0; si < N_SIZES; si++) {
        int n = SIZES[si];
        dlist_build_pooled(&p, n);
        PoolStats ps = dynamic_list_stats(&p);
        printf("  %-10d  %10d  %12zu  %12zu  %9.1f%%\n",
               n,
               ps.n_chunks,
               ps.bytes_allocated / 1024,
               ps.bytes_used      / 1024,
               ps.utilization * 100.0);
        dynamic_list_reset(&p);
    }
}


static void exp2_locality(void) {
    header("EXPERIMENT 2 — Spatial locality (avg stride between nodes)");
    printf("  Lower stride = nodes closer together = fewer cache misses.\n"
           "  Dynamic pool: perfect within chunk, gap at boundary.\n"
           "  Fixed pool:   perfect throughout (always 16 B stride).\n\n");

    int n = 100000;

    DListNode* scattered = dlist_build_scattered(n);
    double stride_scattered = dlist_avg_stride(scattered);

    DynamicListPool dp;
    memset(&dp, 0, sizeof(dp));
    DListNode* dpooled = dlist_build_pooled(&dp, n);
    double stride_dynamic = dlist_avg_stride(dpooled);

    printf("  List size: %d nodes\n\n", n);
    printf("  %-28s  %14s  %20s\n",
           "Variant", "Avg stride", "Nodes per cache line");
    printf("  %-28s  %14s  %20s\n",
           "───────", "──────────", "────────────────────");
    printf("  %-28s  %11.1f B  %20.1f\n",
           "Scattered (malloc/node)",
           stride_scattered,
           64.0 / (stride_scattered > 64 ? stride_scattered : 64));
    printf("  %-28s  %11.1f B  %20.1f\n",
           "Dynamic pool (chunk=64)",
           stride_dynamic,
           stride_dynamic > 0 ? 64.0 / stride_dynamic : 4.0);
    printf("  %-28s  %11.1f B  %20.1f\n",
           "Fixed pool (reference)",
           (double)sizeof(DListNode),
           64.0 / sizeof(DListNode));

    printf("\n  Stride breakdown for dynamic pool:\n");
    printf("  Within a chunk: always %.0f B (nodes[i] to nodes[i+1])\n",
           (double)sizeof(DListNode));
    printf("  At chunk boundary: varies (depends on where malloc\n"
           "  placed the next chunk in the heap)\n");
    printf("  Average %.1f B is pulled up by boundary gaps.\n\n",
           stride_dynamic);

    dlist_free_scattered(scattered);
    dynamic_list_reset(&dp);
}


static void exp3_traversal(void) {
    header("EXPERIMENT 3 — Traversal performance");
    printf("  Dynamic pool should be close to scattered for small sizes\n"
           "  (both fit in cache) and close to fixed pool for large sizes\n"
           "  (chunk locality helps when data spills to DRAM).\n\n");

    printf("  %-10s  %-20s  %-20s  %10s\n",
           "Size", "Scattered (ms)", "Dynamic pool (ms)", "Speedup");
    printf("  %-10s  %-20s  %-20s  %10s\n",
           "────", "──────────────", "─────────────────", "───────");

    int bench_sizes[] = { 10000, 100000, 500000, 1000000 };
    int n_bench = 4;

    DynamicListPool dp;
    memset(&dp, 0, sizeof(dp));
    dynamic_list_init(&dp);

    for (int si = 0; si < n_bench; si++) {
        int n = bench_sizes[si];

        DListNode* scattered = dlist_build_scattered(n);
        DListNode* dpooled   = dlist_build_pooled(&dp, n);

        double t_scattered = bench_list_traverse(scattered);
        double t_dynamic   = bench_list_traverse(dpooled);

        printf("  %-10d  %-20.3f  %-20.3f  %9.2fx\n",
               n, t_scattered, t_dynamic,
               t_scattered / t_dynamic);

        dlist_free_scattered(scattered);
        dynamic_list_reset(&dp);
        dynamic_list_init(&dp);
    }

    dynamic_list_reset(&dp);
}


static void exp4_alloc_throughput(void) {
    header("EXPERIMENT 4 — Allocation throughput");
    printf("  How much faster is dynamic_pool_alloc() vs malloc()?\n"
           "  Pool makes one malloc per %d nodes instead of one per node.\n"
           "  Pool avoids free-list searches, lock overhead, and metadata.\n"
           "  Note: speedup depends on the system malloc implementation.\n"
           "  The malloc() call count reduction is the portable guarantee.\n\n",
           CHUNK_SIZE);

    int bench_sizes[] = { 10000, 100000, 500000, 1000000 };
    int n_bench = 4;

    DynamicListPool dp;
    memset(&dp, 0, sizeof(dp));
    dynamic_list_init(&dp);

    for (int si = 0; si < n_bench; si++) {
        int n = bench_sizes[si];

        double t_scattered = bench_alloc_scattered(n);
        double t_dynamic   = bench_alloc_list(&dp, n);

        double allocs_per_sec_scattered = (double)n / (t_scattered / 1000.0);
        double allocs_per_sec_dynamic   = (double)n / (t_dynamic   / 1000.0);
        double speedup = t_scattered / t_dynamic;

        int calls_scattered = n;
        int calls_dynamic   = (n + CHUNK_SIZE - 1) / CHUNK_SIZE;

        printf("  Allocating %d nodes\n", n);
        printf("  %-32s %10s %18s\n", "Method", "Time (ms)", "Allocs/second");
        printf("  %-32s %10s %18s\n", "------", "---------", "-------------");
        printf("  %-32s %10.3f %15.0f/s\n",
               "malloc() per node",
               t_scattered, allocs_per_sec_scattered);
        printf("  %-32s %10.3f %15.0f/s  (%.1fx faster)\n",
               "dynamic_pool_alloc()",
               t_dynamic, allocs_per_sec_dynamic, speedup);
        printf("  malloc() calls: %d → %d  (%dx fewer)\n\n",
               calls_scattered, calls_dynamic,
               calls_scattered / calls_dynamic);

        dynamic_list_reset(&dp);
        memset(&dp, 0, sizeof(dp));
        dynamic_list_init(&dp);
    }

    dynamic_list_reset(&dp);
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║        DYNAMIC POOL BENCHMARK — Cache-Friendly Allocator          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(DListNode)  = %zu bytes\n", sizeof(DListNode));
    printf("  sizeof(DListChunk) = %zu bytes  (%d nodes + bookkeeping)\n",
           sizeof(DListChunk), CHUNK_SIZE);
    printf("  CHUNK_SIZE         = %d nodes\n", CHUNK_SIZE);
    printf("  Fixed pool size    = %zu MB  (always reserved)\n\n",
           (size_t)(1 << 20) * sizeof(DListNode) / (1024*1024));

    exp1_memory_usage();
    exp2_locality();
    exp3_traversal();
    exp4_alloc_throughput();

    separator();
    printf("\n  SUMMARY\n\n");
    printf("  Fixed pool    — best locality, best speed, worst memory use.\n"
           "                  Right choice for benchmarks and known-size workloads.\n\n");
    printf("  Dynamic pool  — good locality within chunks, scales with use.\n"
           "                  Right choice for production: you don't pay for\n"
           "                  memory you don't use, and you never invalidate\n"
           "                  pointers on growth.\n\n");
    printf("  Scattered     — worst locality, worst speed, highest per-node\n"
           "                  overhead from malloc metadata.\n"
           "                  What the standard library gives you by default.\n\n");

    return 0;
}


