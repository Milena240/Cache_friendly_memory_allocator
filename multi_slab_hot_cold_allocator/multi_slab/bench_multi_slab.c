#define _POSIX_C_SOURCE 200809L
#include "multi_slab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double
now_ms(void) 
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

#define WARMUP_REPS   3
#define MEASURE_REPS  10

static void 
separator(void) 
{
    printf("─────────────────────────────────────────────────────────────────────\n");
}

static void 
header(const char* t)
{
    separator();
    printf("  %s\n", t);
    separator();
}

typedef struct {
    int   id;
    int   counter;
    float value;
    int   flags;
} Obj16;

typedef struct {
    int    id;
    int    counter;
    double score;
    double ratio;
} Obj32;

typedef struct {
    int    id;
    int    counter;
    int    flags;
    int    padding;
    double score;
    double ratio;
    double weight;
    double value;
} Obj64;

typedef struct {
    int    id;
    int    counter;
    int    flags;
    int    padding;
    double scores[12];
} Obj128;

static void
exp1_routing(void)
{
    header("EXPERIMENT 1 — Size class routing");
    printf("  Objects of different sizes automatically go to the\n"
           "  correct size class. No manual selection needed.\n\n");

    MultiSlab* m = mslab_create();

    AllocHandle h16  = mslab_alloc(m, sizeof(Obj16));
    AllocHandle h32  = mslab_alloc(m, sizeof(Obj32));
    AllocHandle h64  = mslab_alloc(m, sizeof(Obj64));
    AllocHandle h128 = mslab_alloc(m, sizeof(Obj128));
    AllocHandle h512 = mslab_alloc(m, 512);   /* too large → malloc */

    printf("  %-12s  %-12s  %-10s  %-10s  %-12s\n",
           "Requested", "Rounded to", "Class idx",
           "Obj/line", "Backed by");
    printf("  %-12s  %-12s  %-10s  %-10s  %-12s\n",
           "─────────", "──────────", "─────────",
           "────────", "─────────");

    struct { size_t req; AllocHandle h; } tests[] = {
        { sizeof(Obj16),  h16  },
        { sizeof(Obj32),  h32  },
        { sizeof(Obj64),  h64  },
        { sizeof(Obj128), h128 },
        { 512,            h512 }
    };
    int ntests = 5;

    for (int i = 0; i < ntests; i++) {
        size_t req = tests[i].req;
        AllocHandle h = tests[i].h;
        int ci  = h.slab_index;
        const char* backed = (ci == -1) ? "malloc" : "slab";
        size_t rounded = (ci >= 0) ? SIZE_CLASS_BYTES[ci] : req;
        int opl = (ci >= 0) ? mslab_objects_per_cache_line(ci) : 0;

        printf("  %-12zu  %-12zu  %-10d  %-10d  %-12s\n",
               req, rounded, ci, opl, backed);
    }
    printf("\n");

    ((Obj16*)  mslab_get_raw(m, h16))->id  = 16;
    ((Obj32*)  mslab_get_raw(m, h32))->id  = 32;
    ((Obj64*)  mslab_get_raw(m, h64))->id  = 64;
    ((Obj128*) mslab_get_raw(m, h128))->id = 128;

    printf("  Verification — reading back id field:\n");
    printf("    Obj16->id  = %d  %s\n",
           ((Obj16*) mslab_get_raw(m, h16))->id,
           ((Obj16*)mslab_get_raw(m, h16))->id == 16 ? "✓" : "✗");
    printf("    Obj32->id  = %d  %s\n",
           ((Obj32*) mslab_get_raw(m, h32))->id,
           ((Obj32*)mslab_get_raw(m, h32))->id == 32 ? "✓" : "✗");
    printf("    Obj64->id  = %d  %s\n",
           ((Obj64*) mslab_get_raw(m, h64))->id,
           ((Obj64*)mslab_get_raw(m, h64))->id == 64 ? "✓" : "✗");
    printf("    Obj128->id = %d  %s\n\n",
           ((Obj128*)mslab_get_raw(m, h128))->id,
           ((Obj128*)mslab_get_raw(m, h128))->id == 128 ? "✓" : "✗");

    mslab_free(m, h16);
    mslab_free(m, h32);
    mslab_free(m, h64);
    mslab_free(m, h128);
    mslab_free(m, h512);
    mslab_destroy(m);
}

static void 
exp2_independent_hotcold(void)
{
    header("EXPERIMENT 2 — Independent hot/cold tracking per size class");
    printf("  Two size classes, each with their own hot objects.\n"
           "  Compaction in one class does not affect the other.\n\n");

    MultiSlab* m = mslab_create();

    int n16  = 50;
    int n64  = 30;
    int hot16_count = 5;  
    int hot64_count = 3; 
    AllocHandle* h16  = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n16);
    AllocHandle* h64  = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n64);

    for (int i = 0; i < n16; i++) {
        h16[i] = mslab_alloc(m, sizeof(Obj16));
        ((Obj16*)mslab_get_raw(m, h16[i]))->id = i;
    }
    for (int i = 0; i < n64; i++) {
        h64[i] = mslab_alloc(m, sizeof(Obj64));
        ((Obj64*)mslab_get_raw(m, h64[i]))->id = i;
    }

    printf("  Allocated %d objects of 16 bytes and %d objects of 64 bytes.\n\n",
           n16, n64);

    long volatile sink = 0;
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 4; rep++) {
        int hi = rep % hot16_count;
        Obj16* o = (Obj16*)mslab_get(m, h16[hi]);
        o->counter++;
        sink += o->counter;
    }

    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 4; rep++) {
        int hi = n64 - hot64_count + (rep % hot64_count);
        Obj64* o = (Obj64*)mslab_get(m, h64[hi]);
        o->counter++;
        sink += o->counter;
    }

    printf("  After accessing:\n");
    printf("    16-byte objects 0..%d heavily\n", hot16_count - 1);
    printf("    64-byte objects %d..%d heavily\n\n",
           n64 - hot64_count, n64 - 1);

    printf("  16-byte slab layout (first 10 slots):\n");
    slab_print_layout(m->slabs[0], 10);

    printf("  64-byte slab layout (first 10 slots):\n");
    slab_print_layout(m->slabs[2], 10);

    printf("  Key observation:\n");
    printf("  The two slabs compacted independently.\n");
    printf("  Hot 16-byte objects are at slots 0..%d of slab_16.\n",
           hot16_count - 1);
    printf("  Hot 64-byte objects are at slots 0..%d of slab_64.\n",
           hot64_count - 1);
    printf("  They do not interfere with each other.\n\n");

    free(h16);
    free(h64);
    mslab_destroy(m);
    (void)sink;
}

static void 
exp3_mixed_performance(void) {
    header("EXPERIMENT 3 — Mixed-size allocation performance");
    printf("  Realistic workload: many 16-byte, some 32/64-byte objects.\n"
           "  20%% of each size class is hot (accessed in tight loops).\n"
           "  Plain: hot objects scattered. Hot-cold: compacted per class.\n\n");

    int counts[] = { 2000, 1000, 500 };  
    size_t sizes[] = { sizeof(Obj16), sizeof(Obj32), sizeof(Obj64) };
    int n_types = 3;
    int hot_frac = 5;   
    int n_scans  = 100;

    printf("  %-8s  %-8s  %-8s  %-14s  %-14s  %10s\n",
           "Size", "Count", "Hot",
           "Plain (ms)", "HotCold (ms)", "Speedup");
    printf("  %-8s  %-8s  %-8s  %-14s  %-14s  %10s\n",
           "────", "─────", "───",
           "──────────", "────────────", "───────");

    double total_speedup = 0;

    for (int ti = 0; ti < n_types; ti++) {
        int    n      = counts[ti];
        size_t sz     = sizes[ti];
        int    n_hot  = n / hot_frac;
        int    ci     = mslab_class_for_size(sz);

        AllocHandle* hot_plain = (AllocHandle*)malloc(
                                     sizeof(AllocHandle) * (size_t)n_hot);
        AllocHandle* hot_hc    = (AllocHandle*)malloc(
                                     sizeof(AllocHandle) * (size_t)n_hot);

        double t_plain = 0;
        {
            MultiSlab* m = mslab_create();

            int hi = 0;
            for (int i = 0; i < n; i++) {
                AllocHandle h = mslab_alloc(m, sz);
                if (i % hot_frac == 0 && hi < n_hot)
                    hot_plain[hi++] = h;
            }

            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int slot = m->slabs[ci]->handle_to_slot[
                                       hot_plain[i].slot_handle];
                        char* ptr = m->slabs[ci]->data +
                                    (size_t)slot * sz;
                        ((Obj16*)ptr)->counter++;
                        sink += ((Obj16*)ptr)->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_plain = total / MEASURE_REPS;

            free(hot_plain);
            mslab_destroy(m);
            (void)sink;
        }

        double t_hotcold = 0;
        {
            MultiSlab* m = mslab_create();

            int hi = 0;
            for (int i = 0; i < n; i++) {
                AllocHandle h = mslab_alloc(m, sz);
                if (i % hot_frac == 0 && hi < n_hot)
                    hot_hc[hi++] = h;
            }

            long volatile sink = 0;
            for (int iter = 0; iter < SLAB_EPOCH_SIZE * 6; iter++) {
                AllocHandle h = hot_hc[iter % n_hot];
                void* ptr = mslab_get(m, h);
                ((Obj16*)ptr)->counter++;
                sink += ((Obj16*)ptr)->counter;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int slot = m->slabs[ci]->handle_to_slot[
                                       hot_hc[i].slot_handle];
                        char* ptr = m->slabs[ci]->data +
                                    (size_t)slot * sz;
                        ((Obj16*)ptr)->counter++;
                        sink += ((Obj16*)ptr)->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_hotcold = total / MEASURE_REPS;

            free(hot_hc);
            mslab_destroy(m);
            (void)sink;
        }

        double speedup = t_plain / t_hotcold;
        total_speedup += speedup;

        printf("  %-8zu  %-8d  %-8d  %-14.3f  %-14.3f  %9.2fx\n",
               sz, n, n_hot, t_plain, t_hotcold, speedup);
    }

    printf("\n  Average speedup across all size classes: %.2fx\n\n",
           total_speedup / n_types);
}

static void
exp4_allocator_summary(void)
{
    header("EXPERIMENT 4 — Full allocator state after mixed workload");
    printf("  Allocate objects of multiple sizes, run a skewed\n"
           "  workload, then print the full allocator state.\n\n");

    MultiSlab* m = mslab_create();

    int n16 = 200, n32 = 100, n64 = 50, n128 = 20;
    AllocHandle* h16  = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n16);
    AllocHandle* h32  = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n32);
    AllocHandle* h64  = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n64);
    AllocHandle* h128 = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n128);

    for (int i = 0; i < n16;  i++) h16[i]  = mslab_alloc(m, sizeof(Obj16));
    for (int i = 0; i < n32;  i++) h32[i]  = mslab_alloc(m, sizeof(Obj32));
    for (int i = 0; i < n64;  i++) h64[i]  = mslab_alloc(m, sizeof(Obj64));
    for (int i = 0; i < n128; i++) h128[i] = mslab_alloc(m, sizeof(Obj128));

    long volatile sink = 0;
    int warmup = SLAB_EPOCH_SIZE * 5;

    for (int rep = 0; rep < warmup; rep++) {
        Obj16* o = (Obj16*)mslab_get(m, h16[rep % 20]);
        o->counter++; sink += o->counter;
    }
    for (int rep = 0; rep < warmup; rep++) {
        Obj32* o = (Obj32*)mslab_get(m, h32[n32 - 10 + rep % 10]);
        o->counter++; sink += o->counter;
    }
    for (int rep = 0; rep < warmup; rep++) {
        Obj64* o = (Obj64*)mslab_get(m, h64[n64/2 + rep % 5]);
        o->counter++; sink += o->counter;
    }

    printf("  Workload complete. Printing allocator state:\n\n");
    mslab_print_summary(m);

    free(h16); free(h32); free(h64); free(h128);
    mslab_destroy(m);
    (void)sink;
}

int
main(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║    MULTI-SIZE HOT-COLD SLAB ALLOCATOR — Cache-Friendly Allocator  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  Size classes and cache line efficiency:\n");
    printf("  %-12s  %-10s  %-14s\n",
           "Size class", "Obj/line", "Slab capacity");
    printf("  %-12s  %-10s  %-14s\n",
           "──────────", "────────", "─────────────");
    for (int i = 0; i < N_SIZE_CLASSES; i++)
        printf("  %-12zu  %-10d  %-14d slots\n",
               SIZE_CLASS_BYTES[i],
               mslab_objects_per_cache_line(i),
               SIZE_CLASS_CAPACITY[i]);
    printf("  Objects > 256 bytes → plain malloc (no slab)\n\n");

    exp1_routing();
    exp2_independent_hotcold();
    exp3_mixed_performance();
    exp4_allocator_summary();

    separator();
    printf("\n  DESIGN SUMMARY\n\n");
    printf("  One slab per size class, each tracking hot/cold independently.\n");
    printf("  mslab_alloc(size) → rounds up → picks correct slab → handle.\n");
    printf("  mslab_get(handle) → finds slab → finds slot → records access.\n");
    printf("  Epoch fires       → compact that slab → hot to front.\n");
    printf("  Other slabs       → unaffected, compact on their own epoch.\n\n");

    return 0;
}
