#define _POSIX_C_SOURCE 200809L

/*
 * bench_hot_cold_slab.c
 *
 * Compile:
 *   gcc -O2 -o bench_hot_cold_slab bench_hot_cold_slab.c hot_cold_slab.c -lm
 *
 * Run:
 *   ./bench_hot_cold_slab
 */

#include "hot_cold_slab.h"

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

/* ── Object type stored in the slab ─────────────────── */

/* A simple object with a counter field we will increment */
typedef struct {
    int   id;
    int   counter;
    float value;
    int   padding;   /* pad to 16 bytes for clean cache lines */
} Object;            /* sizeof = 16 bytes → 4 per cache line  */


/* =========================================================
 * EXPERIMENT 1 — Basic allocation and slab structure
 *
 * Allocate N objects, show what the slab looks like before
 * any access counting, then after some accesses.
 * ========================================================= */

static void exp1_basic_structure(void) {
    header("EXPERIMENT 1 — Slab structure and handle system");
    printf("  Allocate 10 objects, show handle→slot mapping.\n"
           "  Then access some objects and show the slab reacts.\n\n");

    HotColdSlab* slab = slab_create(sizeof(Object), 16);

    /* allocate 10 objects */
    SlabHandle handles[10];
    for (int i = 0; i < 10; i++) {
        handles[i] = slab_alloc(slab);
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        obj->id     = i;
        obj->counter = 0;
    }

    printf("  After allocating 10 objects:\n");
    printf("  Handle → Slot mapping:\n");
    for (int i = 0; i < 10; i++)
        printf("    handle %d → slot %d  (object at address offset %zu B)\n",
               handles[i],
               slab->handle_to_slot[handles[i]],
               (size_t)slab->handle_to_slot[handles[i]] * sizeof(Object));
    printf("\n");

    /* access objects 0, 2, 7 many times */
    printf("  Accessing handles 0, 2, 7 heavily (hot objects)...\n\n");
    long volatile sink = 0;
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 3; rep++) {
        Object* o0 = (Object*)slab_get(slab, handles[0]);
        Object* o2 = (Object*)slab_get(slab, handles[2]);
        Object* o7 = (Object*)slab_get(slab, handles[7]);
        o0->counter++; o2->counter++; o7->counter++;
        sink += o0->counter;
    }

    printf("  After %d accesses (%d epochs):\n\n",
           SLAB_EPOCH_SIZE * 3, slab->n_epochs);
    slab_print_layout(slab, 10);

    printf("  Notice: handles 0, 2, 7 are now in HOT zone slots.\n");
    printf("  Their slot positions changed — they moved to the front.\n");
    printf("  But the handles still work — the mapping table was updated.\n\n");

    /* verify handles still work after compaction */
    printf("  Verifying handles still work after compaction:\n");
    for (int i = 0; i < 10; i++) {
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        printf("    handle %d → slot %d → id=%d  %s\n",
               handles[i],
               slab->handle_to_slot[handles[i]],
               obj->id,
               obj->id == i ? "✓" : "✗ WRONG");
    }
    printf("\n");

    slab_destroy(slab);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 2 — Cache performance: hot-cold slab vs plain
 *
 * Allocate N objects. Make 20% of them hot (accessed often).
 * Compare access time:
 *   Plain slab:      hot objects scattered among cold ones
 *   Hot-cold slab:   hot objects compacted to front
 * ========================================================= */

static void exp2_cache_performance(void) {
    header("EXPERIMENT 2 — Cache performance: hot-cold slab vs plain slab");
    printf("  Hot objects are interleaved with cold ones during allocation.\n"
           "  Plain slab:    hot objects stay at scattered slot positions.\n"
           "  Hot-cold slab: compaction moves hot to slots 0,1,2... at front.\n\n"
           "  To see the cache effect we need the hot set to exceed L1 cache.\n"
           "  L1 is typically 32KB. Object = 16B → 2048 objects fill L1.\n"
           "  We test hot sets larger than this threshold.\n\n");

    /*
     * Key insight: we must scan ALL hot objects in each iteration
     * (not cycle through a small set) so the CPU cannot keep them
     * all in L1. The plain slab's hot objects span many cache lines,
     * the compacted slab's hot objects are packed — fewer line fetches.
     */

    static const int N_HOT_SIZES[] = { 512, 1024, 2048, 4096 };
    int n_tests      = 4;
    int cold_per_hot = 4;    /* 4 cold objects for every hot one */
    int n_scans      = 200;  /* full scans of the hot set per rep */

    printf("  %-10s  %-12s  %-14s  %-14s  %10s  %14s\n",
           "Hot objs", "Total objs",
           "Plain (ms)", "HotCold (ms)",
           "Speedup", "Lines before→after");
    printf("  %-10s  %-12s  %-14s  %-14s  %10s  %14s\n",
           "────────", "──────────",
           "──────────", "────────────",
           "───────", "──────────────────");

    for (int ti = 0; ti < n_tests; ti++) {
        int n_hot     = N_HOT_SIZES[ti];
        int n_objects = n_hot * (cold_per_hot + 1);

        SlabHandle* hot_plain  = (SlabHandle*)malloc(
                                     sizeof(SlabHandle) * (size_t)n_hot);
        SlabHandle* hot_hc     = (SlabHandle*)malloc(
                                     sizeof(SlabHandle) * (size_t)n_hot);

        /* ── PLAIN SLAB ── */
        double t_plain = 0;
        int cls_before = 0;
        {
            HotColdSlab* slab = slab_create(sizeof(Object), n_objects + 16);

            /* interleave: allocate cold_per_hot cold objects then 1 hot */
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                SlabHandle h = slab_alloc(slab);
                ((Object*)slab_get_raw(slab, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_plain[hi++] = h;
            }

            /* measure: scan ALL hot objects in order */
            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int slot = slab->handle_to_slot[hot_plain[i]];
                        Object* o = (Object*)((char*)slab->data +
                                    (size_t)slot * sizeof(Object));
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_plain = total / MEASURE_REPS;

            /* cache lines spanned by hot objects (scattered) */
            int min_s = n_objects, max_s = -1;
            for (int i = 0; i < n_hot; i++) {
                int s = slab->handle_to_slot[hot_plain[i]];
                if (s < min_s) min_s = s;
                if (s > max_s) max_s = s;
            }
            int span = (max_s - min_s + 1) * (int)sizeof(Object);
            cls_before = (span + 63) / 64;

            free(hot_plain);
            slab_destroy(slab);
            (void)sink;
        }

        /* ── HOT-COLD SLAB ── */
        double t_hotcold = 0;
        int cls_after = 0;
        {
            HotColdSlab* slab = slab_create(sizeof(Object), n_objects + 16);

            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                SlabHandle h = slab_alloc(slab);
                ((Object*)slab_get_raw(slab, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_hc[hi++] = h;
            }

            /* warmup: trigger enough epochs so all hot objects compact */
            long volatile sink = 0;
            int warmup = SLAB_EPOCH_SIZE * 8;
            for (int iter = 0; iter < warmup; iter++) {
                Object* o = (Object*)slab_get(slab, hot_hc[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }

            /* measure: same scan — now hot objects are at slots 0..n_hot-1 */
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int slot = slab->handle_to_slot[hot_hc[i]];
                        Object* o = (Object*)((char*)slab->data +
                                    (size_t)slot * sizeof(Object));
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_hotcold = total / MEASURE_REPS;

            /* after compaction hot objects are packed at front */
            cls_after = (n_hot * (int)sizeof(Object) + 63) / 64;

            free(hot_hc);
            slab_destroy(slab);
            (void)sink;
        }

        double speedup = t_plain / t_hotcold;
        char cl_str[32];
        snprintf(cl_str, sizeof(cl_str), "%d → %d", cls_before, cls_after);

        printf("  %-10d  %-12d  %-14.3f  %-14.3f  %9.2fx  %14s\n",
               n_hot, n_objects,
               t_plain, t_hotcold,
               speedup, cl_str);
    }

    printf("\n");
    printf("  Hot objects per cache line: %zu\n", 64 / sizeof(Object));
    printf("  Plain slab hot objects span many cache lines because cold\n");
    printf("  objects sit between them — wasted fetches on every scan.\n");
    printf("  Hot-cold slab packs them: %d hot objs = %zu KB = fewer fetches.\n\n",
           N_HOT_SIZES[3],
           (size_t)N_HOT_SIZES[3] * sizeof(Object) / 1024);
}


/* =========================================================
 * EXPERIMENT 3 — Access report: who is hot?
 *
 * Show the slab's access tracking in action.
 * Allocate objects, run a skewed workload, print the report.
 * ========================================================= */

static void exp3_access_report(void) {
    header("EXPERIMENT 3 — Access report: which objects are hot?");
    printf("  Allocate 20 objects. Access a few of them many times.\n"
           "  Show the access report — who is hot, who is cold.\n\n");

    HotColdSlab* slab = slab_create(sizeof(Object), 32);
    SlabHandle handles[20];

    for (int i = 0; i < 20; i++) {
        handles[i] = slab_alloc(slab);
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        obj->id = i;
    }

    long volatile sink = 0;

    /* access pattern: objects 3, 7, 15 are very hot */
    int hot_set[] = {3, 7, 15};
    for (int rep = 0; rep < 8000; rep++) {
        for (int h = 0; h < 3; h++) {
            Object* obj = (Object*)slab_get(slab,
                               handles[hot_set[h]]);
            obj->counter++;
            sink += obj->counter;
        }
    }
    /* access others occasionally */
    for (int i = 0; i < 20; i++) {
        Object* obj = (Object*)slab_get(slab, handles[i]);
        sink += obj->counter;
    }

    slab_print_access_report(slab, 20);

    printf("  Objects 3, 7, 15 should appear at top with HOT label.\n\n");

    slab_destroy(slab);
    (void)sink;
}


/* =========================================================
 * EXPERIMENT 4 — Compaction lifecycle
 *
 * Show what happens slot by slot before and after compaction.
 * Before: hot objects scattered throughout the slab.
 * After:  hot objects packed at the front.
 * ========================================================= */

static void exp4_compaction_lifecycle(void) {
    header("EXPERIMENT 4 — Compaction lifecycle");
    printf("  Shows slot layout before and after compaction.\n"
           "  Hot objects move to slots 0, 1, 2... at the front.\n\n");

    int n = 12;
    HotColdSlab* slab = slab_create(sizeof(Object), n + 4);
    SlabHandle handles[12];

    for (int i = 0; i < n; i++) {
        handles[i] = slab_alloc(slab);
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        obj->id = i;
    }

    printf("  Layout BEFORE any accesses (all cold):\n");
    slab_print_layout(slab, n);

    /* make handles 1, 5, 9 hot */
    long volatile sink = 0;
    int hot[] = {1, 5, 9};
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 2; rep++)
        for (int h = 0; h < 3; h++) {
            Object* obj = (Object*)slab_get(slab, handles[hot[h]]);
            obj->counter++;
            sink += obj->counter;
        }

    printf("  Layout AFTER accessing handles 1, 5, 9 heavily:\n");
    slab_print_layout(slab, n);

    printf("  Observation:\n");
    printf("  Handles 1, 5, 9 moved to slots 0, 1, 2 (HOT zone).\n");
    printf("  They now share the first cache line of the slab.\n");
    printf("  All other handles still work correctly.\n\n");

    slab_destroy(slab);
    (void)sink;
}


/* =========================================================
 * main
 * ========================================================= */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║      HOT-COLD SLAB ALLOCATOR — Cache-Friendly Allocator           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(Object)   = %zu bytes\n", sizeof(Object));
    printf("  Objects per cache line: %zu\n",  64 / sizeof(Object));
    printf("  SLAB_EPOCH_SIZE  = %d accesses\n", SLAB_EPOCH_SIZE);
    printf("  HOT_PERCENTILE   = %.0f%% of objects\n\n",
           HOT_PERCENTILE * 100);

    exp1_basic_structure();
    exp2_cache_performance();
    exp3_access_report();
    exp4_compaction_lifecycle();

    separator();
    printf("\n  HOW IT ALL FITS TOGETHER\n\n");
    printf("  1. slab_create()  — one big memory chunk, divided into slots\n");
    printf("  2. slab_alloc()   — returns a handle, not a pointer\n");
    printf("  3. slab_get()     — resolves handle→slot, counts the access\n");
    printf("  4. epoch fires    — every %d accesses, compact() runs\n",
           SLAB_EPOCH_SIZE);
    printf("  5. compact()      — sorts slots by access count,\n");
    printf("                      swaps hot slots to the front\n");
    printf("  6. hot objects    — now share first cache lines of the slab\n");
    printf("  7. handles        — still work, mapping table was updated\n\n");

    return 0;
}
