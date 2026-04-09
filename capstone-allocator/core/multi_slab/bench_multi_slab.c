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
exp_header(const char* t)
{
    printf("\n");
    printf("==============================================================\n");
    printf("  %s\n", t);
    printf("==============================================================\n");
    printf("\n");
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
    exp_header("EXP 1: size class routing");
    printf("allocate objects of different sizes, verify each lands in the correct slab\n");
    printf("objects > 256 bytes fall through to malloc\n\n");

    MultiSlab* m = mslab_create();

    AllocHandle h16  = mslab_alloc(m, sizeof(Obj16));
    AllocHandle h32  = mslab_alloc(m, sizeof(Obj32));
    AllocHandle h64  = mslab_alloc(m, sizeof(Obj64));
    AllocHandle h128 = mslab_alloc(m, sizeof(Obj128));
    AllocHandle h512 = mslab_alloc(m, 512);

    printf("%-12s  %-12s  %-10s  %-10s  %s\n",
           "requested", "rounded to", "class idx", "objs/line", "backed by");

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

        printf("%-12zu  %-12zu  %-10d  %-10d  %s\n",
               req, rounded, ci, opl, backed);
    }
    printf("\n");

    ((Obj16*)  mslab_get_raw(m, h16))->id  = 16;
    ((Obj32*)  mslab_get_raw(m, h32))->id  = 32;
    ((Obj64*)  mslab_get_raw(m, h64))->id  = 64;
    ((Obj128*) mslab_get_raw(m, h128))->id = 128;

    printf("id field readback:\n");
    printf("  Obj16->id  = %d  %s\n",
           ((Obj16*) mslab_get_raw(m, h16))->id,
           ((Obj16*)mslab_get_raw(m, h16))->id == 16 ? "ok" : "WRONG");
    printf("  Obj32->id  = %d  %s\n",
           ((Obj32*) mslab_get_raw(m, h32))->id,
           ((Obj32*)mslab_get_raw(m, h32))->id == 32 ? "ok" : "WRONG");
    printf("  Obj64->id  = %d  %s\n",
           ((Obj64*) mslab_get_raw(m, h64))->id,
           ((Obj64*)mslab_get_raw(m, h64))->id == 64 ? "ok" : "WRONG");
    printf("  Obj128->id = %d  %s\n\n",
           ((Obj128*)mslab_get_raw(m, h128))->id,
           ((Obj128*)mslab_get_raw(m, h128))->id == 128 ? "ok" : "WRONG");

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
    exp_header("EXP 2: independent hot/cold tracking per size class");
    printf("heat objects in the 16-byte and 64-byte slabs separately\n");
    printf("each slab should compact independently without affecting the other\n\n");

    MultiSlab* m = mslab_create();

    int n16  = 50;
    int n64  = 30;
    int hot16_count = 5;
    int hot64_count = 3;
    AllocHandle* h16 = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n16);
    AllocHandle* h64 = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n64);

    for (int i = 0; i < n16; i++) {
        h16[i] = mslab_alloc(m, sizeof(Obj16));
        ((Obj16*)mslab_get_raw(m, h16[i]))->id = i;
    }
    for (int i = 0; i < n64; i++) {
        h64[i] = mslab_alloc(m, sizeof(Obj64));
        ((Obj64*)mslab_get_raw(m, h64[i]))->id = i;
    }

    long volatile sink = 0;
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 4; rep++) {
        Obj16* o = (Obj16*)mslab_get(m, h16[rep % hot16_count]);
        o->counter++;
        sink += o->counter;
    }
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 4; rep++) {
        int hi = n64 - hot64_count + (rep % hot64_count);
        Obj64* o = (Obj64*)mslab_get(m, h64[hi]);
        o->counter++;
        sink += o->counter;
    }

    printf("16-byte slab layout (first 10 slots):\n");
    slab_print_layout(m->slabs[0], 10);

    printf("64-byte slab layout (first 10 slots):\n");
    slab_print_layout(m->slabs[2], 10);

    printf("hot 16-byte objects should be at slots 0..%d of slab_16\n", hot16_count - 1);
    printf("hot 64-byte objects should be at slots 0..%d of slab_64\n\n", hot64_count - 1);

    free(h16);
    free(h64);
    mslab_destroy(m);
    (void)sink;
}


static void
exp3_mixed_performance(void)
{
    exp_header("EXP 3: mixed-size allocation performance");
    printf("plain: hot objects stay scattered. multi-slab: compacted per size class.\n");
    printf("20%% of each size class is hot, 100 scans, avg over 10 runs\n\n");

    int counts[]    = { 2000, 1000, 500 };
    size_t sizes[]  = { sizeof(Obj16), sizeof(Obj32), sizeof(Obj64) };
    int n_types     = 3;
    int hot_frac    = 5;
    int n_scans     = 100;

    printf("%-8s  %-8s  %-8s  %-12s  %-12s  %s\n",
           "size", "count", "hot", "plain (ms)", "multi-slab (ms)", "speedup");

    double total_speedup = 0;

    for (int ti = 0; ti < n_types; ti++) {
        int    n     = counts[ti];
        size_t sz    = sizes[ti];
        int    n_hot = n / hot_frac;
        int    ci    = mslab_class_for_size(sz);

        AllocHandle* hot_plain = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n_hot);
        AllocHandle* hot_hc    = (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)n_hot);

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
                        int slot = m->slabs[ci]->handle_to_slot[hot_plain[i].slot_handle];
                        char* ptr = m->slabs[ci]->data + (size_t)slot * sz;
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
                void* ptr = mslab_get(m, hot_hc[iter % n_hot]);
                ((Obj16*)ptr)->counter++;
                sink += ((Obj16*)ptr)->counter;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int slot = m->slabs[ci]->handle_to_slot[hot_hc[i].slot_handle];
                        char* ptr = m->slabs[ci]->data + (size_t)slot * sz;
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

        printf("%-8zu  %-8d  %-8d  %-12.3f  %-15.3f  %.2fx\n",
               sz, n, n_hot, t_plain, t_hotcold, speedup);
    }

    printf("\navg speedup across all size classes: %.2fx\n\n",
           total_speedup / n_types);
}


static void
exp4_allocator_summary(void)
{
    exp_header("EXP 4: allocator state after mixed workload");
    printf("allocate 200/100/50/20 objects across 4 size classes, run skewed accesses\n");
    printf("print the full allocator summary\n\n");

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

    mslab_print_summary(m);

    free(h16); free(h32); free(h64); free(h128);
    mslab_destroy(m);
    (void)sink;
}


int
main(void)
{
    printf("\n");
    printf("*****************************************************************\n");
    printf("*       MULTI-SIZE HOT-COLD SLAB ALLOCATOR -- BENCHMARK         *\n");
    printf("*****************************************************************\n");
    printf("\n");
    printf("size classes and cache line efficiency:\n");
    printf("%-12s  %-10s  %s\n", "size (bytes)", "objs/line", "capacity");
    for (int i = 0; i < N_SIZE_CLASSES; i++)
        printf("%-12zu  %-10d  %d slots\n",
               SIZE_CLASS_BYTES[i],
               mslab_objects_per_cache_line(i),
               SIZE_CLASS_CAPACITY[i]);
    printf("objects > 256 bytes -> plain malloc\n\n");

    exp1_routing();
    exp2_independent_hotcold();
    exp3_mixed_performance();
    exp4_allocator_summary();

    printf("\n=== SUMMARY ===\n\n");
    printf("one slab per size class, each compacting independently\n");
    printf("mslab_alloc(size) rounds up to the nearest class and delegates to that slab\n");
    printf("mslab_get() records the access in the right slab, epoch fires per-slab\n\n");

    return 0;
}
