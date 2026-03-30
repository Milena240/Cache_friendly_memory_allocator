#define _POSIX_C_SOURCE 200809L

/*
 * bench_hotcold.c  —  Hot/Cold struct splitting benchmark
 *
 * Compile:
 *   gcc -O2 -o bench_hotcold bench_hotcold.c hotcold.c -lm
 *
 * Run:
 *   ./bench_hotcold
 *
 * With hardware counters (Linux):
 *   perf stat -e cache-misses,cache-references,L1-dcache-load-misses \
 *       ./bench_hotcold
 */

#include "hotcold.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ── Timing ──────────────────────────────────────────────── */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

/* ── Config ──────────────────────────────────────────────── */

#define WARMUP_REPS   3
#define MEASURE_REPS  10

static const int SIZES[] = { 10000, 100000, 500000, 1000000 };
#define N_SIZES (int)(sizeof(SIZES) / sizeof(SIZES[0]))

/* cold_every values:
 *   0  = pure hot loop (never touch cold fields)
 *   10 = touch cold every 10th element  (10% cold access)
 *   2  = touch cold every 2nd element   (50% cold access)  */
static const int COLD_RATES[] = { 0, 10, 2 };
static const char* COLD_LABELS[] = {
    "0% cold access  (hot only)",
    "10% cold access (1 in 10)",
    "50% cold access (1 in 2) "
};
#define N_RATES (int)(sizeof(COLD_RATES) / sizeof(COLD_RATES[0]))

/* ── Helpers ─────────────────────────────────────────────── */

static void separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}

static void header(const char* title) {
    separator();
    printf("  %s\n", title);
    separator();
}

/* Run one timed measurement, return minimum time in ms */
#define BENCH(result_var, code)                          \
    do {                                                 \
        long volatile _sink = 0;                         \
        (result_var) = 1e18;                             \
        for (int _r = 0; _r < WARMUP_REPS + MEASURE_REPS; _r++) { \
            double _t0 = now_ms();                       \
            _sink += (code);                             \
            double _t1 = now_ms();                       \
            if (_r >= WARMUP_REPS && (_t1-_t0) < (result_var)) \
                (result_var) = _t1 - _t0;               \
        }                                                \
        (void)_sink;                                     \
    } while(0)

/* =========================================================
 * EXPERIMENT 1 — Pure hot loop, varying array size
 *
 * cold_every = 0: never touch cold fields.
 * This is the best-case scenario for splitting —
 * the unsplit version wastes cache on cold fields
 * even though they are never read.
 * ========================================================= */

static void exp1_pure_hot(void) {
    header("EXPERIMENT 1 — Pure hot loop (cold fields never accessed)");
    printf("  The loop only touches counter and flags.\n"
           "  Unsplit still loads all 10 fields per cache line.\n"
           "  Split loads only the 8-byte hot struct.\n\n");

    printf("  %-12s  %-26s %10s %10s %10s %10s\n",
           "Size", "Layout", "Time(ms)", "Speedup",
           "Obj/line", "MB touched");
    printf("  %-12s  %-26s %10s %10s %10s %10s\n",
           "────", "──────", "────────", "───────",
           "────────", "──────────");

    for (int si = 0; si < N_SIZES; si++) {
        int n = SIZES[si];

        ObjectUnsplit* u   = build_unsplit(n);
        ObjectSplit*   sh  = build_split_hot(n);
        ColdData*      sc  = build_split_cold(n, sh);
        ObjectPadded*  p   = build_padded(n);

        double t_u, t_s, t_p;
        BENCH(t_u, workload_unsplit(u,  n, 0));
        BENCH(t_s, workload_split  (sh, n, 0));
        BENCH(t_p, workload_padded (p,  n, 0));

        /* MB of hot data actually needed */
        double mb_unsplit = (double)n * sizeof(ObjectUnsplit) / 1e6;
        double mb_split   = (double)n * sizeof(ObjectSplit)   / 1e6;

        printf("  %-12d  %-26s %10.3f %10s %10zu %9.2f MB\n",
               n, "Unsplit (baseline)",
               t_u, "1.00x",
               64 / sizeof(ObjectUnsplit) == 0 ? (size_t)1 : 64/sizeof(ObjectUnsplit),
               mb_unsplit);
        printf("  %-12s  %-26s %10.3f %9.2fx %10zu %9.2f MB\n",
               "", "Split (hot+cold ptr)",
               t_s, t_u / t_s,
               64 / sizeof(ObjectSplit),
               mb_split);
        printf("  %-12s  %-26s %10.3f %9.2fx %10zu %9.2f MB\n\n",
               "", "Padded (reorder only)",
               t_p, t_u / t_p,
               64 / sizeof(ObjectPadded) == 0 ? (size_t)1 : 64/sizeof(ObjectPadded),
               mb_unsplit);

        free_unsplit(u);
        free_split(sh, sc);
        free_padded(p);
    }
}

/* =========================================================
 * EXPERIMENT 2 — Varying cold access rate
 *
 * Shows the tradeoff: splitting is very beneficial when
 * cold fields are never/rarely accessed. But as cold access
 * rate rises, the split version pays the cost of following
 * an extra pointer every iteration — and eventually the
 * unsplit version catches up.
 * ========================================================= */

static void exp2_cold_rate(void) {
    header("EXPERIMENT 2 — Effect of cold access rate (1M objects)");
    printf("  At 0%% cold: split wins big (cold data never loaded).\n"
           "  At 50%% cold: split advantage shrinks or reverses\n"
           "  (pointer dereference to cold data becomes expensive).\n\n");

    int n = 1000000;

    ObjectUnsplit* u  = build_unsplit(n);
    ObjectSplit*   sh = build_split_hot(n);
    ColdData*      sc = build_split_cold(n, sh);
    ObjectPadded*  p  = build_padded(n);

    printf("  %-30s %12s %12s %12s\n",
           "Cold access rate", "Unsplit", "Split", "Padded");
    printf("  %-30s %12s %12s %12s\n",
           "────────────────", "───────", "─────", "──────");

    for (int ri = 0; ri < N_RATES; ri++) {
        int cold = COLD_RATES[ri];
        double t_u, t_s, t_p;
        BENCH(t_u, workload_unsplit(u,  n, cold));
        BENCH(t_s, workload_split  (sh, n, cold));
        BENCH(t_p, workload_padded (p,  n, cold));

        printf("  %-30s %9.3f ms %9.3f ms %9.3f ms\n",
               COLD_LABELS[ri], t_u, t_s, t_p);
        printf("  %-30s %12s %11.2fx %11.2fx\n",
               "", "1.00x", t_u/t_s, t_u/t_p);
        printf("\n");
    }

    free_unsplit(u);
    free_split(sh, sc);
    free_padded(p);
}

/* =========================================================
 * EXPERIMENT 3 — Cache line efficiency
 *
 * Directly shows how many useful objects fit per cache line
 * for each layout, and what that means for total cache lines
 * needed to process the whole array.
 * ========================================================= */

static void exp3_cache_efficiency(void) {
    header("EXPERIMENT 3 — Cache line efficiency analysis");
    printf("  How many objects fit in one 64-byte cache line?\n"
           "  More objects per line = fewer lines needed = fewer misses.\n\n");

    /* Sizes of what the hot loop actually touches */
    size_t hot_size_unsplit = sizeof(ObjectUnsplit);  /* loads everything */
    /* hot_size_split would be sizeof(ObjectSplit) - sizeof(ColdData*) = 8 bytes
     * but we just use sizeof(ObjectSplit) for the footprint calculation below */
    size_t hot_size_padded  = sizeof(ObjectPadded);

    printf("  %-22s %12s %12s %12s\n",
           "Metric", "Unsplit", "Split", "Padded");
    printf("  %-22s %12s %12s %12s\n",
           "──────", "───────", "─────", "──────");

    printf("  %-22s %11zu B %11zu B %11zu B\n",
           "Struct size",
           sizeof(ObjectUnsplit),
           sizeof(ObjectSplit),
           sizeof(ObjectPadded));

    printf("  %-22s %11zu B %11zu B %11zu B\n",
           "Hot fields size",
           (size_t)8, (size_t)8, (size_t)8);

    printf("  %-22s %11zu   %11zu   %11zu\n",
           "Objects per cache line",
           64 / hot_size_unsplit == 0 ? (size_t)1 : 64/hot_size_unsplit,
           64 / sizeof(ObjectSplit),
           64 / hot_size_padded  == 0 ? (size_t)1 : 64/hot_size_padded);

    int n = 1000000;
    printf("  %-22s %9zu K   %9zu K   %9zu K\n",
           "Cache lines for 1M obj",
           ((size_t)n * hot_size_unsplit) / 64 / 1000,
           ((size_t)n * sizeof(ObjectSplit)) / 64 / 1000,
           ((size_t)n * hot_size_padded) / 64 / 1000);

    printf("  %-22s %9.1f MB %9.1f MB %9.1f MB\n",
           "Memory footprint (hot)",
           (double)n * hot_size_unsplit / 1e6,
           (double)n * sizeof(ObjectSplit) / 1e6,
           (double)n * hot_size_padded / 1e6);

    printf("\n");
    printf("  The split layout uses %.1fx less memory for the hot loop\n"
           "  compared to unsplit → fits %.1fx more data in the same cache.\n\n",
           (double)hot_size_unsplit / sizeof(ObjectSplit),
           (double)hot_size_unsplit / sizeof(ObjectSplit));
}

/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║         HOT/COLD SPLITTING BENCHMARK — Cache-Friendly Allocator   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    print_struct_sizes();

    exp1_pure_hot();
    exp2_cold_rate();
    exp3_cache_efficiency();

    separator();
    printf("\n  HOW TO READ THESE RESULTS\n\n");
    printf("  Experiment 1 — The speedup of Split over Unsplit shows how much\n"
           "  cache space is wasted by cold fields sitting next to hot fields.\n"
           "  Padded (reorder only) should be similar to Unsplit — moving\n"
           "  fields around inside one struct doesn't reduce its total size.\n\n");
    printf("  Experiment 2 — Watch the Split speedup shrink as cold access\n"
           "  rate rises. When cold data is accessed often, the extra pointer\n"
           "  dereference becomes a bottleneck. The split is a tradeoff, not\n"
           "  a free lunch.\n\n");
    printf("  Experiment 3 — The raw numbers behind the speedup: how many\n"
           "  objects per cache line, and how much memory the hot loop touches.\n"
           "  This is what you cite in your paper.\n\n");

    return 0;
}
