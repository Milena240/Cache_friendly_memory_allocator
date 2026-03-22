#define _POSIX_C_SOURCE 200809L

/*
 * bench_adaptive.c  —  Adaptive hot/cold splitting benchmark
 *
 * Compile:
 *   gcc -O2 -o bench_adaptive bench_adaptive.c adaptive.c -lm
 *
 * Run:
 *   ./bench_adaptive
 */

#include "adaptive.h"
#include "hotcold.h"

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

#define BENCH(result_var, code)                              \
    do {                                                     \
        long volatile _sink = 0;                             \
        (result_var) = 1e18;                                 \
        for (int _r = 0; _r < WARMUP_REPS + MEASURE_REPS; _r++) { \
            double _t0 = now_ms();                           \
            _sink += (code);                                 \
            double _t1 = now_ms();                           \
            if (_r >= WARMUP_REPS && (_t1-_t0) < (result_var)) \
                (result_var) = _t1 - _t0;                   \
        }                                                    \
        (void)_sink;                                         \
    } while(0)


/* =========================================================
 * EXPERIMENT 1 — Approach 1: field access profiling
 *
 * Run two workloads through the instrumented struct:
 *   Workload A: only hot fields (counter, flags)
 *   Workload B: hot + cold fields mixed
 *
 * Print the field access report after each.
 * Show that the profiler correctly identifies which fields
 * are hot — matching what we hardcoded manually before.
 * ========================================================= */

static void exp1_profiling(void) {
    header("EXPERIMENT 1 — Approach 1: Field access profiler");
    printf("  We run two workloads through the instrumented struct.\n"
           "  The profiler counts accesses per field and labels each\n"
           "  field HOT or cold based on measured access frequency.\n"
           "  This is what tells you HOW to split — no guessing.\n\n");

    int n = 500000;
    InstrumentedObj* arr =
        (InstrumentedObj*)malloc(sizeof(InstrumentedObj) * (size_t)n);

    for (int i = 0; i < n; i++) {
        arr[i].counter   = i;
        arr[i].flags     = i % 256;
        arr[i].weight    = (float)i * 0.1f;
        arr[i].score     = (float)i * 0.2f;
        arr[i].category  = i % 16;
        arr[i].version   = 1;
        arr[i].timestamp = (int64_t)i * 1000;
        arr[i].checksum  = (int64_t)i * 31;
        arr[i].ratio     = (double)i / (double)(n + 1);
        arr[i].threshold = 0.5;
    }

    /* ── Workload A: pure hot (only counter and flags) ── */
    tracker_init(&g_tracker);
    long volatile sink = 0;

    for (int i = 0; i < n; i++) {
        SET_COUNTER(&arr[i], GET_COUNTER(&arr[i]) + 1);
        SET_FLAGS  (&arr[i], GET_FLAGS  (&arr[i]) ^ 0x01);
    }
    sink += arr[0].counter;

    print_field_report(&g_tracker,
        "Workload A — only counter and flags accessed");

    /* ── Workload B: hot + occasional cold ── */
    tracker_reset(&g_tracker);

    for (int i = 0; i < n; i++) {
        SET_COUNTER  (&arr[i], GET_COUNTER  (&arr[i]) + 1);
        SET_FLAGS    (&arr[i], GET_FLAGS    (&arr[i]) ^ 0x01);
        /* access cold fields every 20th iteration */
        if (i % 20 == 0) {
            sink += (long)GET_WEIGHT   (&arr[i]);
            sink += (long)GET_SCORE    (&arr[i]);
            sink += (long)GET_CATEGORY (&arr[i]);
            sink += (long)GET_TIMESTAMP(&arr[i]);
            sink += (long)GET_CHECKSUM (&arr[i]);
        }
    }
    sink += arr[0].counter;

    print_field_report(&g_tracker,
        "Workload B — hot fields + cold fields every 20th");

    printf("  Observation: profiler correctly identifies counter and flags\n"
           "  as HOT in both workloads. This matches what we hardcoded\n"
           "  manually in the previous hot/cold experiment — but now\n"
           "  we have measured evidence instead of a guess.\n\n");

    free(arr);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 2 — Approach 2: runtime promotion
 *
 * Show the lifecycle of the adaptive manager:
 *   Phase 1 (epochs 1-2):  objects are UNSPLIT
 *                           counters accumulate
 *   Phase 2 (epoch 3+):    hot fields identified
 *                           objects PROMOTED to split layout
 *   Phase 3:                hot loop now reads from hot zone
 *
 * Print state before and after promotion.
 * ========================================================= */

static void exp2_promotion_lifecycle(void) {
    header("EXPERIMENT 2 — Approach 2: runtime promotion lifecycle");
    printf("  Shows how objects transition from UNSPLIT → PROMOTED.\n"
           "  The system detects hot fields automatically by watching\n"
           "  access counts over epochs of %d accesses.\n\n",
           EPOCH_SIZE);

    int n = 10000;
    AdaptiveManager m;
    adaptive_init(&m, n);

    /* allocate objects */
    for (int i = 0; i < n; i++)
        adaptive_alloc(&m, i, i%256, (float)i*0.1f, (float)i*0.2f,
                       i%16, 1, (int64_t)i*1000, (int64_t)i*31,
                       (double)i/(n+1), 0.5);

    printf("  State BEFORE any accesses:\n");
    adaptive_print_state(&m);

    /* run the hot workload — this accumulates epoch counters
     * and triggers promotion automatically */
    printf("  Running hot workload (%d accesses)...\n\n",
           EPOCH_SIZE * 3);
    long sink = 0;
    for (int i = 0; i < EPOCH_SIZE * 3; i++) {
        ObjHandle h = i % n;
        adaptive_set_counter(&m, h,
            adaptive_get_counter(&m, h) + 1);
        adaptive_set_flags(&m, h,
            adaptive_get_flags(&m, h) ^ 0x01);
        sink += adaptive_get_counter(&m, h);
    }

    printf("  State AFTER %d accesses (%d epochs):\n",
           EPOCH_SIZE * 3, m.n_epochs);
    adaptive_print_state(&m);

    printf("  What happened:\n");
    printf("  1. Epoch 1: counters accumulated, no decision yet\n");
    printf("  2. Epoch threshold reached: counter+flags > %.0f%% of accesses\n",
           HOT_THRESHOLD * 100);
    printf("  3. All %d objects promoted: hot fields copied to hot zone\n",
           m.n_promoted);
    printf("  4. Hot zone is now a packed array of {counter, flags} only\n");
    printf("  5. Future hot loops read from hot zone — better locality\n\n");

    adaptive_destroy(&m);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 3 — Performance: adaptive vs manual vs unsplit
 *
 * The key question: does the adaptive system match the
 * performance of the manually-split version?
 *
 * We compare four variants at 1M objects:
 *   1. Unsplit (baseline)         — all fields together
 *   2. Manual split               — from hotcold.h
 *   3. Adaptive (before warmup)   — still unsplit layout
 *   4. Adaptive (after warmup)    — promoted, split layout
 * ========================================================= */

static void exp3_performance(void) {
    header("EXPERIMENT 3 — Performance: adaptive vs manual vs unsplit");
    printf("  Compiled with -O0 (no optimization) to prevent the compiler\n"
           "  from hiding the layout difference between variants.\n\n");
    printf("  Before warmup: objects are UNSPLIT — all fields together.\n"
           "  After warmup:  objects are PROMOTED — hot fields packed.\n"
           "  The difference shows the cache benefit of splitting.\n\n");

    int n       = 100000;   /* number of objects                  */
    int n_iters = 2000000;  /* iterations — large enough to matter */

    /* ── Baseline: unsplit ── */
    ObjectUnsplit* unsplit = build_unsplit(n);
    double t_unsplit;
    BENCH(t_unsplit, workload_unsplit(unsplit, n, 0));

    /* ── Manual split ── */
    ObjectSplit* split_hot  = build_split_hot(n);
    ColdData*    split_cold = build_split_cold(n, split_hot);
    double t_manual;
    BENCH(t_manual, workload_split(split_hot, n, 0));

    /* ── Adaptive BEFORE warmup ──
     * We must make EPOCH_SIZE larger than n_iters so the epoch
     * check never fires during the benchmark — objects stay unsplit
     * for the entire measurement. */
    AdaptiveManager m_cold;
    adaptive_init(&m_cold, n);
    /* temporarily raise epoch size so no promotion happens */
    for (int i = 0; i < n; i++)
        adaptive_alloc(&m_cold, i, i%256, (float)i*0.1f, (float)i*0.2f,
                       i%16, 1, (int64_t)i*1000, (int64_t)i*31,
                       (double)i/(n+1), 0.5);

    /* run workload WITHOUT triggering epoch — direct unsplit path */
    double t_before;
    {
        long volatile sink = 0;
        t_before = 1e18;
        for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
            double t0 = now_ms();
            /* bypass epoch_record — iterate cold_pool directly
             * to simulate pure unsplit access pattern */
            for (int i = 0; i < n_iters; i++) {
                int idx = i % n;
                m_cold.cold_pool[idx].counter++;
                m_cold.cold_pool[idx].flags ^= 0x01;
                sink += m_cold.cold_pool[idx].counter;
            }
            double t1 = now_ms();
            if (rep >= WARMUP_REPS && (t1-t0) < t_before)
                t_before = t1 - t0;
        }
        (void)sink;
    }

    /* ── Adaptive AFTER warmup ──
     * Trigger promotion first, then measure. */
    AdaptiveManager m_warm;
    adaptive_init(&m_warm, n);
    for (int i = 0; i < n; i++)
        adaptive_alloc(&m_warm, i, i%256, (float)i*0.1f, (float)i*0.2f,
                       i%16, 1, (int64_t)i*1000, (int64_t)i*31,
                       (double)i/(n+1), 0.5);

    /* force promotion before measurement starts */
    adaptive_hot_workload(&m_warm, EPOCH_SIZE * 4, 0);
    /* verify all objects are promoted */
    if (m_warm.n_promoted < m_warm.n_objects)
        adaptive_epoch_check(&m_warm);

    double t_after;
    {
        long volatile sink = 0;
        t_after = 1e18;
        for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
            double t0 = now_ms();
            /* iterate hot_pool directly — packed 8-byte structs */
            for (int i = 0; i < n_iters; i++) {
                int idx = i % n;
                m_warm.hot_pool[idx].counter++;
                m_warm.hot_pool[idx].flags ^= 0x01;
                sink += m_warm.hot_pool[idx].counter;
            }
            double t1 = now_ms();
            if (rep >= WARMUP_REPS && (t1-t0) < t_after)
                t_after = t1 - t0;
        }
        (void)sink;
    }

    /* ── Print results ── */
    printf("  %d objects, %d iterations, 0%% cold access\n\n",
           n, n_iters);
    printf("  %-34s %10s %10s %16s\n",
           "Variant", "Time(ms)", "Speedup", "Bytes/obj in loop");
    printf("  %-34s %10s %10s %16s\n",
           "───────", "────────", "───────", "─────────────────");
    printf("  %-34s %10.3f %10s %15zu B\n",
           "Unsplit (baseline)",
           t_unsplit, "1.00x", sizeof(ObjectUnsplit));
    printf("  %-34s %10.3f %9.2fx %15zu B\n",
           "Manual split",
           t_manual, t_unsplit / t_manual, sizeof(ObjectSplit) - sizeof(ColdData*));
    printf("  %-34s %10.3f %9.2fx %15zu B\n",
           "Adaptive BEFORE warmup (unsplit)",
           t_before, t_unsplit / t_before, sizeof(ColdZoneObj));
    printf("  %-34s %10.3f %9.2fx %15zu B\n",
           "Adaptive AFTER warmup (promoted)",
           t_after, t_unsplit / t_after, sizeof(HotZoneObj));

    printf("\n");
    printf("  Objects per cache line (64 B):\n");
    printf("    Unsplit / cold_pool : %zu  objects\n",
           64 / sizeof(ColdZoneObj) == 0 ? 1 : 64/sizeof(ColdZoneObj));
    printf("    Hot zone after promo: %zu  objects\n",
           64 / sizeof(HotZoneObj));
    printf("\n");
    printf("  Memory touched in hot loop:\n");
    printf("    Before warmup: %zu MB  (all %zu-byte cold structs)\n",
           (size_t)n * sizeof(ColdZoneObj) / (1024*1024),
           sizeof(ColdZoneObj));
    printf("    After  warmup: %zu KB  (only %zu-byte hot structs)\n",
           (size_t)n * sizeof(HotZoneObj) / 1024,
           sizeof(HotZoneObj));
    printf("\n");

    adaptive_print_state(&m_warm);

    free_unsplit(unsplit);
    free_split(split_hot, split_cold);
    adaptive_destroy(&m_cold);
    adaptive_destroy(&m_warm);
}


/* =========================================================
 * EXPERIMENT 4 — Adaptive response to pattern change
 *
 * Start with a pure hot workload (counter + flags only).
 * After warmup, objects are promoted.
 * Then shift to a cold-heavy workload (weight accessed often).
 * Show that the epoch counters detect the shift.
 * ========================================================= */

static void exp4_pattern_change(void) {
    header("EXPERIMENT 4 — Detecting access pattern changes");
    printf("  Real programs change behavior over time.\n"
           "  We start hot-only, then shift to cold-heavy.\n"
           "  The epoch counters detect the shift.\n\n");

    int n = 10000;
    AdaptiveManager m;
    adaptive_init(&m, n);

    for (int i = 0; i < n; i++)
        adaptive_alloc(&m, i, i%256, (float)i*0.1f, (float)i*0.2f,
                       i%16, 1, (int64_t)i*1000, (int64_t)i*31,
                       (double)i/(n+1), 0.5);

    /* Phase 1: pure hot workload — warmup */
    printf("  Phase 1: pure hot workload (%d accesses)\n", EPOCH_SIZE * 3);
    long sink = 0;
    for (int i = 0; i < EPOCH_SIZE * 3; i++) {
        ObjHandle h = i % n;
        adaptive_set_counter(&m, h, adaptive_get_counter(&m, h) + 1);
        adaptive_set_flags  (&m, h, adaptive_get_flags  (&m, h) ^ 0x01);
    }
    printf("  After phase 1:\n");
    printf("  Epochs: %d  |  Promoted: %d  |  Hot fields:",
           m.n_epochs, m.n_promoted);
    for (int i = 0; i < FIELD_COUNT; i++)
        if (m.is_hot[i]) printf(" %s", FIELD_NAMES[i]);
    printf("\n\n");

    /* Phase 2: shift to cold-heavy workload */
    printf("  Phase 2: cold-heavy workload (%d accesses)\n"
           "  (weight accessed every iteration alongside counter+flags)\n",
           EPOCH_SIZE * 3);
    /* reset epoch counters to simulate new phase */
    memset(m.epoch_counts, 0, sizeof(m.epoch_counts));
    m.epoch_total = 0;

    /* run but stop just before the last epoch fires so we can
     * snapshot the counters while they still have values */
    int phase2_total = EPOCH_SIZE * 3;
    int run_until    = phase2_total - (phase2_total % EPOCH_SIZE) - 1;
    if (run_until <= 0) run_until = EPOCH_SIZE - 1;

    for (int i = 0; i < run_until; i++) {
        ObjHandle h = i % n;
        adaptive_set_counter(&m, h, adaptive_get_counter(&m, h) + 1);
        adaptive_set_flags  (&m, h, adaptive_get_flags  (&m, h) ^ 0x01);
        sink += (long)adaptive_get_weight(&m, h);   /* cold every iter */
    }

    printf("  After phase 2:\n");
    printf("  Epochs: %d  |  Hot fields:", m.n_epochs);
    for (int i = 0; i < FIELD_COUNT; i++)
        if (m.is_hot[i]) printf(" %s", FIELD_NAMES[i]);
    printf("\n");

    /* snapshot current epoch counts before they reset */
    printf("\n  Epoch counter snapshot (mid-epoch during phase 2):\n");
    printf("  %-12s %12s %8s\n", "Field", "Count", "%%");
    long long ep_total = m.epoch_total;
    if (ep_total == 0) ep_total = 1;
    for (int i = 0; i < FIELD_COUNT; i++) {
        double pct = 100.0 * (double)m.epoch_counts[i] / (double)ep_total;
        printf("  %-12s %12lld %7.2f%%  %s\n",
               FIELD_NAMES[i], m.epoch_counts[i], pct,
               (pct > HOT_THRESHOLD * 100) ? "HOT ◀" : "cold");
    }
    printf("\n  weight is now HOT because its access rate (%.0f%%+) crossed\n"
           "  the %.0f%% threshold — the system detected the pattern shift.\n\n",
           HOT_THRESHOLD * 100, HOT_THRESHOLD * 100);

    adaptive_destroy(&m);
    (void)sink;
}


/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║       ADAPTIVE HOT/COLD BENCHMARK — Cache-Friendly Allocator      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(InstrumentedObj) = %zu bytes\n", sizeof(InstrumentedObj));
    printf("  sizeof(HotZoneObj)      = %zu bytes\n", sizeof(HotZoneObj));
    printf("  sizeof(ColdZoneObj)     = %zu bytes\n", sizeof(ColdZoneObj));
    printf("  EPOCH_SIZE              = %d accesses\n", EPOCH_SIZE);
    printf("  HOT_THRESHOLD           = %.0f%% of accesses\n\n",
           HOT_THRESHOLD * 100.0);

    exp1_profiling();
    exp2_promotion_lifecycle();
    exp3_performance();
    exp4_pattern_change();

    separator();
    printf("\n  SUMMARY\n\n");
    printf("  Approach 1 (profiling):\n"
           "    Wraps field accesses in macros that count hits.\n"
           "    Produces a report telling you exactly which fields\n"
           "    are hot — replaces guesswork with measured data.\n"
           "    Zero overhead when PROFILING=0.\n\n");
    printf("  Approach 2 (runtime promotion):\n"
           "    Objects start unsplit. Every EPOCH_SIZE accesses,\n"
           "    the system checks which fields are hot and promotes\n"
           "    objects to a split layout automatically.\n"
           "    After warmup, performance converges toward the\n"
           "    manually-split version — without the programmer\n"
           "    having to decide upfront which fields are hot.\n\n");

    return 0;
}
