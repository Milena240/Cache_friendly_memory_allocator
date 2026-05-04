#define _POSIX_C_SOURCE 200809L

#include "array_profiler.h"

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

#define ARRAY_SIZE   1000
#define HOT_COUNT    20
#define HOT_ACCESSES 10000
#define COLD_ACCESSES 1

static int HOT_INDICES[HOT_COUNT] = {
    742, 13, 567, 891, 234, 456, 123, 678, 345, 789,
    99,  501, 777, 42,  333, 888, 256, 614, 155, 970
};

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

static void workload_hot_only(int* arr, int n,
                               long volatile* sink) {
    (void)n;
    for (int rep = 0; rep < HOT_ACCESSES * 10; rep++)
        for (int h = 0; h < HOT_COUNT; h++)
            *sink += arr[HOT_INDICES[h]];
}

static void workload_front_only(int* arr, int n,
                                 long volatile* sink) {
    (void)n;
    for (int rep = 0; rep < HOT_ACCESSES * 10; rep++)
        for (int h = 0; h < HOT_COUNT; h++)
            *sink += arr[h];
}


static void exp1_profiling(void) {
    header("EXPERIMENT 1 — Array element access profiling");
    printf("  Array size: %d elements\n", ARRAY_SIZE);
    printf("  Hot elements: %d indices accessed %d times each\n",
           HOT_COUNT, HOT_ACCESSES);
    printf("  Cold elements: %d indices accessed once each\n\n",
           ARRAY_SIZE - HOT_COUNT);

    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);

    long volatile sink = 0;
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);

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


static void exp2_reordering(void) {
    header("EXPERIMENT 2 — Reordering hot elements to front");
    printf("  After profiling we know which elements are hot.\n"
           "  We reorder the array: hot elements go to indices 0..%d.\n"
           "  They now all fit in the first %d cache lines.\n\n",
           HOT_COUNT - 1,
           (HOT_COUNT * (int)sizeof(int) + 63) / 64);

    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);
    long volatile sink = 0;
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);

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


static void exp3_performance(void) {
    header("EXPERIMENT 3 — Performance before vs after reordering");
    printf("  Accessing %d hot elements %d times each.\n\n",
           HOT_COUNT, HOT_ACCESSES * 10);

    int* arr = (int*)malloc(sizeof(int) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = i * 10;

    long volatile sink = 0;
    double t_before = 0, t_after = 0;

    for (int r = 0; r < WARMUP_REPS + MEASURE_REPS; r++) {
        double t0 = now_ms();
        workload_hot_only(arr, ARRAY_SIZE, &sink);
        double t1 = now_ms();
        if (r >= WARMUP_REPS) t_before += t1 - t0;
    }
    t_before /= MEASURE_REPS;

    ArrayProfiler p;
    profiler_init(&p, ARRAY_SIZE);
    workload_skewed_profiled(&p, arr, ARRAY_SIZE, &sink);
    ReorderedArray* r = reorder_hot_first(&p, arr, 0.001);

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

    int cls_before = HOT_COUNT;
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


static void exp4_patterns(void) {
    header("EXPERIMENT 4 — Different access patterns");
    printf("  Shows profiler correctly identifies hot elements\n"
           "  regardless of the access pattern shape.\n\n");

    int n   = 200;
    int* arr = (int*)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) arr[i] = i;

    ArrayProfiler p;
    long volatile sink = 0;

    profiler_init(&p, n);
    int hot_count_a = n / 5;
    for (int rep = 0; rep < 1000; rep++)
        for (int i = 0; i < hot_count_a; i++)
            sink += GET_ELEM(&p, arr, i);
    for (int rep = 0; rep < 200; rep++)
        for (int i = hot_count_a; i < n; i++)
            sink += GET_ELEM(&p, arr, i);

    printf("  Pattern A: 80/20 — first %d elements get most accesses\n",
           hot_count_a);
    print_access_report(&p, "80/20 pattern", 10);
    profiler_destroy(&p);

    profiler_init(&p, n);
    for (int rep = 0; rep < 50000; rep++)
        sink += GET_ELEM(&p, arr, 137);
    for (int i = 0; i < n; i++)
        sink += GET_ELEM(&p, arr, i);

    printf("  Pattern B: single hotspot — index 137 accessed 50000 times\n");
    print_access_report(&p, "single hotspot", 5);
    profiler_destroy(&p);

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
