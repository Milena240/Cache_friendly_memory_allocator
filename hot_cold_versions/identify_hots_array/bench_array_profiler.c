#define _POSIX_C_SOURCE 200809L

/*
 * bench_array_profiler.c  —  Array element access profiler benchmark
 *
 * Compile:
 *   gcc -O2 -o bench_array_profiler bench_array_profiler.c array_profiler.c -lm
 *
 * Run:
 *   ./bench_array_profiler
 */

#include "array_profiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

/* ── Timing ──────────────────────────────────────────── */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

#define WARMUP_REPS   3
#define MEASURE_REPS  10

static void separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}
static void header(const char* t) {
    separator();
    printf("  %s\n", t);
    separator();
}


/* =========================================================
 * Workloads
 *
 * workload_skewed: simulates a real program where a small
 *   number of elements (the "hot set") are accessed very
 *   often, and the rest are rarely touched.
 *   Example: a cache of user records where the top 10
 *   users account for 90% of all accesses.
 *
 * workload_sequential: accesses every element once in order.
 *   Used to measure traversal speed after reordering.
 *
 * workload_hot_only: accesses only the first N elements.
 *   Used to compare hot-element access before and after
 *   reordering — before reordering hot elements are
 *   scattered, after reordering they are at indices 0..N.
 * ========================================================= */

#define ARRAY_SIZE   1000    /* total elements in array    */
#define HOT_COUNT    20      /* how many elements are hot  */
#define HOT_ACCESSES 10000   /* times each hot elem accessed */
#define COLD_ACCESSES 1      /* times each cold elem accessed */

/* the hot indices — fixed set, simulating frequently
 * accessed records, popular items, etc. */
static int HOT_INDICES[HOT_COUNT] = {
    742, 13, 567, 891, 234, 456, 123, 678, 345, 789,
    99,  501, 777, 42,  333, 888, 256, 614, 155, 970
};

/* same workload but through profiler macros */
static void workload_skewed_profiled(ArrayProfiler* p,
                                      int* arr, int n,
                                      long volatile* sink) {
    for (int h = 0; h < HOT_COUNT; h++)
        for (int rep = 0; rep < HOT_ACCESSES; rep++)
            *sink += GET_ELEM(p, arr, HOT_INDICES[h]);

    for (int i = 0; i < n; i++) {
        int is_hot = 0;
        for (int h = 0; h < HOT_COUNT; h++)
            if (i == HOT_INDICES[h]) { is_hot = 1; break; }
        if (!is_hot)
            *sink += GET_ELEM(p, arr, i);
    }
}

/* access only the hot elements — used to measure
 * how fast we can reach hot data before/after reorder */
static void workload_hot_only(int* arr, int n,
                               long volatile* sink) {
    (void)n;
    for (int rep = 0; rep < HOT_ACCESSES * 10; rep++)
        for (int h = 0; h < HOT_COUNT; h++)
            *sink += arr[HOT_INDICES[h]];
}

/* access the first HOT_COUNT elements — used after
 * reordering when hot elements are at indices 0..HOT_COUNT */
static void workload_front_only(int* arr, int n,
                                 long volatile* sink) {
    (void)n;
    for (int rep = 0; rep < HOT_ACCESSES * 10; rep++)
        for (int h = 0; h < HOT_COUNT; h++)
            *sink += arr[h];
}


/* =========================================================
 * EXPERIMENT 1 — Access profiling
 *
 * Run the skewed workload through the profiler.
 * Show which indices are hot and which are cold.
 * The profiler should identify exactly our HOT_INDICES.
 * ========================================================= */

static void exp1_profiling(void) {
    header("EXPERIMENT 1 — Array element access profiling");
    printf("  Array size: %d elements\n", ARRAY_SIZE);
    printf("  Hot elements: %d indices accessed %d times each\n",
           HOT_COUNT, HOT_ACCESSES);
    printf("  Cold elements: %d indices accessed once each\n\n",
           ARRAY_SIZE - HOT_COUNT);

    /* build array */
    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    /* profile the workload */
    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);

    long volatile sink = 0;
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);

    /* print report — show top 25 hottest elements */
    print_access_report(&p, "skewed workload", 25);

    printf("  Verification: the profiler should show indices\n");
    printf("  ");
    for (int h = 0; h < HOT_COUNT; h++)
        printf("%d ", HOT_INDICES[h]);
    printf("\n  as HOT — these are the indices we accessed %d times.\n\n",
           HOT_ACCESSES);

    profiler_destroy(&p);
    free(arr);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 2 — Reordering: hot elements to front
 *
 * Take the profiled access counts.
 * Reorder the array so the hottest elements are at
 * indices 0, 1, 2, ... HOT_COUNT-1.
 * Show the mapping: which old index moved where.
 * ========================================================= */

static void exp2_reordering(void) {
    header("EXPERIMENT 2 — Reordering hot elements to front");
    printf("  After profiling we know which elements are hot.\n"
           "  We reorder the array: hot elements go to indices 0..%d.\n"
           "  They now all fit in the first %d cache lines.\n\n",
           HOT_COUNT - 1,
           (HOT_COUNT * (int)sizeof(int) + 63) / 64);

    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    /* profile */
    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);
    long volatile sink = 0;
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);

    /* reorder */
    ReorderedArray* r = reorder_hot_first(&p, arr, 0.001);

    printf("  Before reordering — hot elements scattered:\n");
    printf("  Hot indices: ");
    for (int h = 0; h < HOT_COUNT; h++)
        printf("%d ", HOT_INDICES[h]);
    printf("\n");
    printf("  These span cache lines: ");
    int prev_cl = -1;
    for (int h = 0; h < HOT_COUNT; h++) {
        int cl = (HOT_INDICES[h] * (int)sizeof(int)) / 64;
        if (cl != prev_cl) { printf("%d ", cl); prev_cl = cl; }
    }
    printf("\n\n");

    printf("  After reordering — hot elements at front:\n");
    print_reorder_map(r, HOT_COUNT + 5);

    profiler_destroy(&p);
    reordered_destroy(r);
    free(arr);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 3 — Performance: before vs after reordering
 *
 * Measure how much faster it is to access the hot elements
 * after they have been moved to the front of the array.
 *
 * Before reorder: hot elements at scattered indices —
 *   each access may be a separate cache line
 * After reorder:  hot elements at indices 0..HOT_COUNT-1 —
 *   all fit in first ceil(HOT_COUNT * 4 / 64) cache lines
 * ========================================================= */

static void exp3_performance(void) {
    header("EXPERIMENT 3 — Performance before vs after reordering");
    printf("  Accessing %d hot elements %d times each.\n\n",
           HOT_COUNT, HOT_ACCESSES * 10);

    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    /* ── Before reordering ── */
    long volatile sink = 0;
    double t_before = 0, t_after = 0;

    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        workload_hot_only(arr, ARRAY_SIZE, &sink);
        double t1 = now_ms();
        if (r >= WARMUP_REPS) t_before += t1 - t0;
    }
    t_before /= MEASURE_REPS;

    /* ── Profile then reorder ── */
    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);
    ReorderedArray* r = reorder_hot_first(&p, arr, 0.001);

    /* ── After reordering ── */
    for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
        double t0 = now_ms();
        workload_front_only(r->data, ARRAY_SIZE, &sink);
        double t1 = now_ms();
        if (rep >= WARMUP_REPS) t_after += t1 - t0;
    }
    t_after /= MEASURE_REPS;

    double speedup = t_before / t_after;

    printf("  %-38s %10s %14s\n",
           "Variant", "Avg (ms)", "Cache lines used");
    printf("  %-38s %10s %14s\n",
           "───────", "────────", "────────────────");

    int cls_before = HOT_COUNT;   /* worst case: one per element */
    int cls_after  = (HOT_COUNT * (int)sizeof(int) + 63) / 64;

    printf("  %-38s %10.3f %14d\n",
           "Before reorder (scattered hot elements)",
           t_before, cls_before);
    printf("  %-38s %10.3f %14d\n",
           "After reorder  (hot elements at front)",
           t_after, cls_after);
    printf("\n  Speedup: %.2fx\n\n", speedup);

    printf("  Why it is faster:\n");
    printf("  Before: %d hot elements scattered across up to %d "
           "cache lines.\n", HOT_COUNT, cls_before);
    printf("  After:  %d hot elements packed into %d cache line(s).\n",
           HOT_COUNT, cls_after);
    printf("  All hot data fits in L1 cache after reordering.\n\n");

    profiler_destroy(&p);
    reordered_destroy(r);
    free(arr);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 4 — Different access patterns
 *
 * Shows that the profiler adapts to whatever the actual
 * access pattern is — not just our hardcoded hot set.
 * We use three different patterns:
 *   A: 80/20 rule — 20% of elements get 80% of accesses
 *   B: single hotspot — one element accessed almost always
 *   C: uniform — all elements accessed equally
 * ========================================================= */

static void exp4_patterns(void) {
    header("EXPERIMENT 4 — Different access patterns");
    printf("  Shows profiler correctly identifies hot elements\n"
           "  regardless of the access pattern shape.\n\n");

    int n   = 200;
    int* arr = (int*)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) arr[i] = i;

    ArrayProfiler p;
    long volatile sink = 0;

    /* ── Pattern A: 80/20 rule ── */
    profiler_init(&p, n);
    int hot_count_a = n / 5;   /* 20% of elements */
    for (int rep = 0; rep < 1000; rep++)
        for (int i = 0; i < hot_count_a; i++)   /* access 20% often */
            sink += GET_ELEM(&p, arr, i);
    for (int rep = 0; rep < 200; rep++)
        for (int i = hot_count_a; i < n; i++)   /* access 80% rarely */
            sink += GET_ELEM(&p, arr, i);

    printf("  Pattern A: 80/20 — first %d elements get most accesses\n",
           hot_count_a);
    print_access_report(&p, "80/20 pattern", 10);
    profiler_destroy(&p);

    /* ── Pattern B: single hotspot ── */
    profiler_init(&p, n);
    for (int rep = 0; rep < 50000; rep++)
        sink += GET_ELEM(&p, arr, 137);          /* one element only */
    for (int i = 0; i < n; i++)
        sink += GET_ELEM(&p, arr, i);            /* rest once each */

    printf("  Pattern B: single hotspot — index 137 accessed 50000 times\n");
    print_access_report(&p, "single hotspot", 5);
    profiler_destroy(&p);

    /* ── Pattern C: uniform ── */
    profiler_init(&p, n);
    for (int rep = 0; rep < 100; rep++)
        for (int i = 0; i < n; i++)
            sink += GET_ELEM(&p, arr, i);

    printf("  Pattern C: uniform — all elements accessed equally\n");
    print_access_report(&p, "uniform access", 5);
    profiler_destroy(&p);

    free(arr);
    (void)sink;
}


/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║      ARRAY ELEMENT PROFILER — Cache-Friendly Allocator            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(int)    = %zu bytes\n", sizeof(int));
    printf("  Cache line     = 64 bytes\n");
    printf("  Ints per line  = %zu\n\n", 64 / sizeof(int));

    exp1_profiling();
    exp2_reordering();
    exp3_performance();
    exp4_patterns();

    separator();
    printf("\n  SUMMARY\n\n");
    printf("  The array profiler works exactly like the struct field\n"
           "  profiler — but for array indices instead of field names.\n\n");
    printf("  Step 1: wrap accesses in GET_ELEM/SET_ELEM macros\n");
    printf("  Step 2: run your workload — counters accumulate\n");
    printf("  Step 3: call print_access_report() — see hot indices\n");
    printf("  Step 4: call reorder_hot_first() — move hot to front\n");
    printf("  Step 5: hot elements now fit in first few cache lines\n\n");
    printf("  Set ARRAY_PROFILING=0 to compile away all overhead.\n\n");

    return 0;
}
