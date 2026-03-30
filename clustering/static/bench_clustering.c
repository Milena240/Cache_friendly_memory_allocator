#define _POSIX_C_SOURCE 200809L

#include "node_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void print_separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}

static void print_header(const char* title) {
    print_separator();
    printf("  %s\n", title);
    print_separator();
}

static ListPool g_list_pool;
static TreePool g_tree_pool;

#define WARMUP_REPS    3
#define MEASURE_REPS  10   /* repeat each timed section, take the minimum */

static const int LIST_SIZES[] = { 10000, 100000, 500000, 1000000 };
static const int TREE_SIZES[] = { 10000, 100000, 500000, 1000000 };

static void bench_list_traversal(int size) {
    printf("\n  List size: %d nodes\n\n", size);
    printf("  %-28s %12s %12s %14s\n",
           "Variant", "Time (ms)", "Speedup", "Avg stride");
    printf("  %-28s %12s %12s %14s\n",
           "-------", "---------", "-------", "----------");

    double t_scattered_min = 1e18;
    double t_pooled_min    = 1e18;
    long   volatile sink   = 0;   /* prevent dead-code elimination */

    ListNode* scattered_head = NULL;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        if (scattered_head) list_free_scattered(scattered_head);
        scattered_head = list_build_scattered(size);

        double t0 = now_ms();
        sink += list_traverse_sum(scattered_head);
        double t1 = now_ms();

        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_scattered_min) t_scattered_min = t1 - t0;
    }
    double stride_scattered = list_avg_stride(scattered_head);

    list_pool_init(&g_list_pool);
    ListNode* pooled_head = NULL;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        pooled_head = list_build_pooled(&g_list_pool, size);

        double t0 = now_ms();
        sink += list_traverse_sum(pooled_head);
        double t1 = now_ms();

        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_pooled_min) t_pooled_min = t1 - t0;
    }
    double stride_pooled = list_avg_stride(pooled_head);

    double speedup = t_scattered_min / t_pooled_min;

    printf("  %-28s %12.3f %12s %11.1f B\n",
           "Scattered (malloc/node)",
           t_scattered_min, "1.00x", stride_scattered);
    printf("  %-28s %12.3f %11.2fx %11.1f B\n",
           "Pooled (contiguous array)",
           t_pooled_min, speedup, stride_pooled);

    int nodes_per_cl_scattered = (stride_scattered > 0)
        ? (int)(64.0 / stride_scattered + 0.5) : 1;
    int nodes_per_cl_pooled = (int)(64.0 / sizeof(ListNode));
    printf("\n  Cache-line (%d bytes) holds ~%d scattered node(s) vs ~%d pooled node(s)\n",
           64, nodes_per_cl_scattered < 1 ? 1 : nodes_per_cl_scattered,
           nodes_per_cl_pooled);

    list_free_scattered(scattered_head);
    (void)sink;
}

static double bench_tree_traversal(int size) {
    printf("\n  Tree size: %d nodes\n\n", size);
    printf("  %-28s %12s %12s %14s\n",
           "Variant", "Avg time(ms)", "Speedup", "Avg stride");
    printf("  %-28s %12s %12s %14s\n",
           "-------", "------------", "-------", "----------");

    long volatile sink = 0;

    double times_scattered[MEASURE_REPS];
    double times_pooled   [MEASURE_REPS];

    TreeNode* scattered_root = tree_build_scattered(size);

    for (int r = 0; r < WARMUP_REPS; r++)
        sink += tree_traverse_count(scattered_root);

    for (int r = 0; r < MEASURE_REPS; r++) {
        double t0 = now_ms();
        sink += tree_traverse_count(scattered_root);
        double t1 = now_ms();
        times_scattered[r] = t1 - t0;
    }

    double sum_s = 0;
    for (int r = 0; r < MEASURE_REPS; r++) sum_s += times_scattered[r];
    double avg_scattered = sum_s / MEASURE_REPS;

    double stride_scattered = tree_avg_stride(scattered_root, size);

    tree_pool_init(&g_tree_pool);
    TreeNode* pooled_root = tree_build_pooled(&g_tree_pool, size);

    for (int r = 0; r < WARMUP_REPS; r++)
        sink += tree_traverse_count(pooled_root);

    for (int r = 0; r < MEASURE_REPS; r++) {
        double t0 = now_ms();
        sink += tree_traverse_count(pooled_root);
        double t1 = now_ms();
        times_pooled[r] = t1 - t0;
    }

    double sum_p = 0;
    for (int r = 0; r < MEASURE_REPS; r++) sum_p += times_pooled[r];
    double avg_pooled = sum_p / MEASURE_REPS;

    double stride_pooled = tree_avg_stride(pooled_root, size);
    double speedup = avg_scattered / avg_pooled;

    printf("  %-28s %12.3f %12s %11.1f B\n",
           "Scattered (malloc/node)",
           avg_scattered, "1.00x", stride_scattered);
    printf("  %-28s %12.3f %11.2fx %11.1f B\n",
           "Pooled (contiguous array)",
           avg_pooled, speedup, stride_pooled);

    printf("\n  Individual rep times (ms) — shows consistency:\n");
    printf("  %-28s", "Scattered:");
    for (int r = 0; r < MEASURE_REPS; r++)
        printf(" %6.3f", times_scattered[r]);
    printf("\n");
    printf("  %-28s", "Pooled:");
    for (int r = 0; r < MEASURE_REPS; r++)
        printf(" %6.3f", times_pooled[r]);
    printf("\n");

    tree_free_scattered(scattered_root);
    (void)sink;
    return speedup;   /* return so main can accumulate for average */
}

static double bench_tree_search(int size) {
    printf("\n  Tree size: %d nodes\n\n", size);
    printf("  %-28s %12s %12s %14s\n",
           "Variant", "Avg time(ms)", "Speedup", "Avg stride");
    printf("  %-28s %12s %12s %14s\n",
           "-------", "------------", "-------", "----------");

    int   n_searches = size / 7 + 1;
    long  volatile sink = 0;

    double times_scattered[MEASURE_REPS];
    double times_pooled   [MEASURE_REPS];

    TreeNode* scattered_root = tree_build_scattered(size);

    for (int r = 0; r < WARMUP_REPS; r++)
        for (int i = 0; i < n_searches; i++)
            sink += tree_search(scattered_root, (i * 7) % size);

    for (int r = 0; r < MEASURE_REPS; r++) {
        double t0 = now_ms();
        for (int i = 0; i < n_searches; i++)
            sink += tree_search(scattered_root, (i * 7) % size);
        double t1 = now_ms();
        times_scattered[r] = t1 - t0;
    }

    double sum_s = 0;
    for (int r = 0; r < MEASURE_REPS; r++) sum_s += times_scattered[r];
    double avg_scattered = sum_s / MEASURE_REPS;
    double stride_scattered = tree_avg_stride(scattered_root, size);

    tree_pool_init(&g_tree_pool);
    TreeNode* pooled_root = tree_build_pooled(&g_tree_pool, size);

    for (int r = 0; r < WARMUP_REPS; r++)
        for (int i = 0; i < n_searches; i++)
            sink += tree_search(pooled_root, (i * 7) % size);

    for (int r = 0; r < MEASURE_REPS; r++) {
        double t0 = now_ms();
        for (int i = 0; i < n_searches; i++)
            sink += tree_search(pooled_root, (i * 7) % size);
        double t1 = now_ms();
        times_pooled[r] = t1 - t0;
    }

    double sum_p = 0;
    for (int r = 0; r < MEASURE_REPS; r++) sum_p += times_pooled[r];
    double avg_pooled = sum_p / MEASURE_REPS;
    double stride_pooled = tree_avg_stride(pooled_root, size);

    double speedup = avg_scattered / avg_pooled;

    printf("  %-28s %12.3f %12s %11.1f B\n",
           "Scattered (malloc/node)",
           avg_scattered, "1.00x", stride_scattered);
    printf("  %-28s %12.3f %11.2fx %11.1f B\n",
           "Pooled (contiguous array)",
           avg_pooled, speedup, stride_pooled);

    printf("\n  Individual rep times (ms):\n");
    printf("  %-28s", "Scattered:");
    for (int r = 0; r < MEASURE_REPS; r++)
        printf(" %6.3f", times_scattered[r]);
    printf("\n");
    printf("  %-28s", "Pooled:");
    for (int r = 0; r < MEASURE_REPS; r++)
        printf(" %6.3f", times_pooled[r]);
    printf("\n");

    tree_free_scattered(scattered_root);
    (void)sink;
    return speedup;
}

static ListNode* compact_list(ListNode* head, ListPool* pool) {
    list_pool_reset(pool);
    ListNode* new_head = NULL;
    ListNode* new_tail = NULL;
    while (head) {
        ListNode* n = list_pool_alloc(pool, head->value);
        if (!new_head) new_head = n;
        else           new_tail->next = n;
        new_tail = n;
        head = head->next;
    }
    return new_head;
}

static void bench_compaction(int size) {
    printf("\n  List size: %d nodes\n\n", size);
    printf("  %-34s %12s %12s\n",
           "Variant", "Time (ms)", "Speedup");
    printf("  %-34s %12s %12s\n",
           "-------", "---------", "-------");

    long volatile sink = 0;

    ListNode* scattered = list_build_scattered(size);

    double t_before_min = 1e18;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        sink += list_traverse_sum(scattered);
        double t1 = now_ms();
        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_before_min) t_before_min = t1 - t0;
    }

    list_pool_init(&g_list_pool);
    ListNode* compacted = compact_list(scattered, &g_list_pool);

    double t_after_min = 1e18;
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        sink += list_traverse_sum(compacted);
        double t1 = now_ms();
        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_after_min) t_after_min = t1 - t0;
    }

    double speedup = t_before_min / t_after_min;

    printf("  %-34s %12.3f %12s\n",
           "Before compaction (scattered)", t_before_min, "1.00x");
    printf("  %-34s %12.3f %11.2fx\n",
           "After compaction (pool copy)", t_after_min, speedup);

    double stride_before = list_avg_stride(scattered);
    double stride_after  = list_avg_stride(compacted);
    printf("\n  Avg stride before: %.1f B   after: %.1f B\n",
           stride_before, stride_after);

    list_free_scattered(scattered);
    (void)sink;
}


static void bench_alloc_throughput(int size) {
    printf("\n  Allocating %d nodes\n\n", size);
    printf("  %-28s %12s %18s\n",
           "Method", "Time (ms)", "Allocs/second");
    printf("  %-28s %12s %18s\n",
           "------", "---------", "-------------");

    double t_malloc_min = 1e18;
    double t_pool_min   = 1e18;

    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        ListNode* head = NULL, *tail = NULL;
        double t0 = now_ms();
        for (int i = 0; i < size; i++) {
            ListNode* n = (ListNode*)malloc(sizeof(ListNode));
            n->value = i; n->next = NULL;
            if (!head) head = n; else tail->next = n;
            tail = n;
        }
        double t1 = now_ms();
        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_malloc_min) t_malloc_min = t1 - t0;
        list_free_scattered(head);
    }

    list_pool_init(&g_list_pool);
    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        list_pool_reset(&g_list_pool);
        double t0 = now_ms();
        for (int i = 0; i < size; i++)
            list_pool_alloc(&g_list_pool, i);
        double t1 = now_ms();
        if (r >= WARMUP_REPS)
            if ((t1 - t0) < t_pool_min) t_pool_min = t1 - t0;
    }

    double malloc_tput = (double)size / (t_malloc_min / 1000.0);
    double pool_tput   = (double)size / (t_pool_min   / 1000.0);

    printf("  %-28s %12.3f %15.0f/s\n",
           "malloc() per node", t_malloc_min, malloc_tput);
    printf("  %-28s %12.3f %15.0f/s  (%.1fx faster)\n",
           "pool_alloc()", t_pool_min, pool_tput,
           pool_tput / malloc_tput);
}


int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║          CLUSTERING BENCHMARK — Cache-Friendly Allocator          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(ListNode) = %zu bytes\n", sizeof(ListNode));
    printf("  sizeof(TreeNode) = %zu bytes\n", sizeof(TreeNode));
    printf("  Cache line size  = 64 bytes (assumed)\n");
    printf("  Nodes per cache line (list): %zu\n", 64 / sizeof(ListNode));
    printf("  Nodes per cache line (tree): %zu\n\n", 64 / sizeof(TreeNode));

    print_header("EXPERIMENT 1 — Linked List Traversal (sum all values)");
    printf("  Measures pointer-chasing performance.\n"
           "  Scattered nodes → each step is likely a cache miss.\n"
           "  Pooled nodes    → multiple nodes per cache line.\n");
    for (int i = 0; i < (int)(sizeof(LIST_SIZES)/sizeof(LIST_SIZES[0])); i++)
        bench_list_traversal(LIST_SIZES[i]);

    int n_tree_sizes = (int)(sizeof(TREE_SIZES)/sizeof(TREE_SIZES[0]));

    double traversal_speedups[4];
    double search_speedups   [4];

    print_header("EXPERIMENT 2a — Binary Search Tree Traversal (count all nodes)");
    printf("  Visits every node in the tree.\n"
           "  Shows average time across %d runs so variance is visible.\n"
           "  Pool-built trees have better spatial locality → fewer misses.\n",
           MEASURE_REPS);
    for (int i = 0; i < n_tree_sizes; i++)
        traversal_speedups[i] = bench_tree_traversal(TREE_SIZES[i]);

    print_header("EXPERIMENT 2b — Binary Search Tree Searches");
    printf("  Repeated lookups stress the pointer-chasing path.\n"
           "  Shows average time across %d runs.\n",
           MEASURE_REPS);
    for (int i = 0; i < n_tree_sizes; i++)
        search_speedups[i] = bench_tree_search(TREE_SIZES[i]);

    print_header("EXPERIMENT 3 — Manual Compaction (reorder demo)");
    printf("  Simulates what a compacting GC does:\n"
           "  takes a scattered list built with malloc, copies it\n"
           "  into a pool in traversal order, measures the speedup.\n");
    bench_compaction(500000);
    bench_compaction(1000000);

    print_header("EXPERIMENT 4 — Allocation Throughput");
    printf("  How much faster is pool_alloc() vs malloc()?\n"
           "  Pool avoids syscalls, free-list searches, and metadata.\n");
    bench_alloc_throughput(500000);

    print_separator();
    printf("\n  HOW TO READ THESE RESULTS\n\n");
    printf("  Avg stride   — mean byte-distance between consecutive nodes.\n");
    printf("                 < 64 B  → nodes share cache lines (good)\n");
    printf("                 > 64 B  → every step is a new cache line (bad)\n\n");
    printf("  Speedup      — how many times faster the pooled version is.\n");
    printf("                 Larger lists show bigger speedups because\n");
    printf("                 they don't fit in L2/L3 and scattered nodes\n");
    printf("                 cause actual DRAM fetches.\n\n");
    printf("  Compaction   — demonstrates why compacting GCs improve\n");
    printf("                 performance even without changing the algorithm.\n\n");

    print_separator();
    printf("\n  TREE POOL vs MALLOC — AVERAGE SPEEDUP SUMMARY\n\n");

    printf("  %-12s  %14s  %14s\n",
           "Tree size", "Traversal", "Search");
    printf("  %-12s  %14s  %14s\n",
           "─────────", "─────────", "──────");
    for (int i = 0; i < n_tree_sizes; i++)
        printf("  %-12d  %13.2fx  %13.2fx\n",
               TREE_SIZES[i],
               traversal_speedups[i],
               search_speedups[i]);

    double avg_traversal = 0, avg_search = 0;
    for (int i = 0; i < n_tree_sizes; i++) {
        avg_traversal += traversal_speedups[i];
        avg_search    += search_speedups[i];
    }
    avg_traversal /= n_tree_sizes;
    avg_search    /= n_tree_sizes;

    printf("  %-12s  %14s  %14s\n",
           "─────────", "─────────", "──────────");
    printf("  %-12s  %13.2fx  %13.2fx\n",
           "AVERAGE", avg_traversal, avg_search);

    printf("\n  Pool allocator is on average %.2fx faster than malloc\n"
           "  for tree traversal and %.2fx faster for tree search.\n\n",
           avg_traversal, avg_search);

    printf("  Why traversal speedup > search speedup:\n"
           "  Traversal visits every node in order → pool locality helps.\n"
           "  Search jumps to random positions → both layouts miss similarly.\n\n");

    return 0;
}


