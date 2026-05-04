#define _POSIX_C_SOURCE 200809L
#include "hot_cold_slab.h"

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

static void exp_header(const char* t) {
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
    int   padding;
} Object;


static double measure_hot_scan_hc(HotColdSlab* slab,
                                   SlabHandle* hot_hs, int n_hot,
                                   int scans)
{
    long volatile sink = 0;
    double total = 0;
    for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
        double t0 = now_ms();
        for (int sc = 0; sc < scans; sc++)
            for (int i = 0; i < n_hot; i++) {
                int slot = slab->handle_to_slot[hot_hs[i]];
                Object* o = (Object*)((char*)slab->data +
                            (size_t)slot * sizeof(Object));
                o->counter++;
                sink += o->counter;
            }
        double t1 = now_ms();
        if (rep >= WARMUP_REPS) total += t1 - t0;
    }
    (void)sink;
    return total / MEASURE_REPS;
}


static void exp1_basic_structure(void)
{
    exp_header("EXP 1: slab structure and handle system");
    printf("allocate 10 objects, hammer handles 0/2/7, verify all handles survive compaction\n\n");

    HotColdSlab* slab = slab_create(sizeof(Object), 16);

    SlabHandle handles[10];
    for (int i = 0; i < 10; i++) {
        handles[i] = slab_alloc(slab);
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        obj->id     = i;
        obj->counter = 0;
    }

    printf("after allocating 10 objects:\n");
    for (int i = 0; i < 10; i++)
        printf("  handle %d -> slot %d  (offset %zu B)\n",
               handles[i],
               slab->handle_to_slot[handles[i]],
               (size_t)slab->handle_to_slot[handles[i]] * sizeof(Object));
    printf("\n");

    long volatile sink = 0;
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 3; rep++) {
        Object* o0 = (Object*)slab_get(slab, handles[0]);
        Object* o2 = (Object*)slab_get(slab, handles[2]);
        Object* o7 = (Object*)slab_get(slab, handles[7]);
        o0->counter++; o2->counter++; o7->counter++;
        sink += o0->counter;
    }

    printf("after %d accesses (%d epochs):\n\n", SLAB_EPOCH_SIZE * 3, slab->n_epochs);
    slab_print_layout(slab, 10);

    printf("handles still valid after compaction:\n");
    for (int i = 0; i < 10; i++) {
        Object* obj = (Object*)slab_get_raw(slab, handles[i]);
        printf("  handle %d -> slot %d  id=%d  %s\n",
               handles[i],
               slab->handle_to_slot[handles[i]],
               obj->id,
               obj->id == i ? "ok" : "WRONG");
    }
    printf("\n");

    slab_destroy(slab);
    (void)sink;
}


static void exp2_cache_performance(void)
{
    exp_header("EXP 2: cache performance -- hot-cold vs plain slab");
    printf("hot objects stay scattered in plain slab, compacted to front in hot-cold\n");
    printf("200 scans of hot set only, avg over 10 runs\n\n");

    static const int N_HOT_SIZES[] = { 512, 1024, 2048, 4096 };
    int n_tests      = 4;
    int cold_per_hot = 4;
    int n_scans      = 200;

    printf("%-10s  %-12s  %-12s  %-14s  %-10s  cache lines\n",
           "hot objs", "total objs", "plain (ms)", "hot-cold (ms)", "speedup");

    for (int ti = 0; ti < n_tests; ti++) {
        int n_hot     = N_HOT_SIZES[ti];
        int n_objects = n_hot * (cold_per_hot + 1);

        SlabHandle* hot_plain = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)n_hot);
        SlabHandle* hot_hc    = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)n_hot);

        double t_plain = 0;
        int cls_before = 0;
        {
            HotColdSlab* slab = slab_create(sizeof(Object), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                SlabHandle h = slab_alloc(slab);
                ((Object*)slab_get_raw(slab, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_plain[hi++] = h;
            }

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

            long volatile sink = 0;
            int warmup = SLAB_EPOCH_SIZE * 8;
            for (int iter = 0; iter < warmup; iter++) {
                Object* o = (Object*)slab_get(slab, hot_hc[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }

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
            cls_after = (n_hot * (int)sizeof(Object) + 63) / 64;

            free(hot_hc);
            slab_destroy(slab);
            (void)sink;
        }

        double speedup = t_plain / t_hotcold;
        char cl_str[32];
        char sp_str[16];
        snprintf(cl_str, sizeof(cl_str), "%d -> %d", cls_before, cls_after);
        snprintf(sp_str, sizeof(sp_str), "%.2fx", speedup);

        printf("%-10d  %-12d  %-12.3f  %-14.3f  %-10s  %s\n",
               n_hot, n_objects, t_plain, t_hotcold, sp_str, cl_str);
    }

    printf("\nhot-cold slab packs hot objects at the front so every cache line loaded\n");
    printf("is useful data. in the plain case cold objects sit between hot ones.\n\n");
}



static void exp4_compaction_lifecycle(void)
{
    exp_header("EXP 4: compaction lifecycle");
    printf("show slot layout before and after compaction\n");
    printf("hot objects should move to slots 0, 1, 2 at the front\n\n");

    int n = 12;
    HotColdSlab* slab = slab_create(sizeof(Object), n + 4);
    SlabHandle handles[12];

    for (int i = 0; i < n; i++) {
        handles[i] = slab_alloc(slab);
        ((Object*)slab_get_raw(slab, handles[i]))->id = i;
    }

    printf("layout before any accesses:\n");
    slab_print_layout(slab, n);

    long volatile sink = 0;
    int hot[] = {1, 5, 9};
    for (int rep = 0; rep < SLAB_EPOCH_SIZE * 2; rep++)
        for (int h = 0; h < 3; h++) {
            Object* obj = (Object*)slab_get(slab, handles[hot[h]]);
            obj->counter++;
            sink += obj->counter;
        }

    printf("layout after hammering handles 1, 5, 9:\n");
    slab_print_layout(slab, n);

    printf("handles 1, 5, 9 should now be at slots 0, 1, 2\n");
    printf("they share the first cache line, all other handles still work\n\n");

    slab_destroy(slab);
    (void)sink;
}


static void exp5_epoch_performance_progression(void)
{
    exp_header("EXP 5: scan time per epoch");
    printf("8000 objects, 10%% hot. watch scan time drop as hot objects pack to the front.\n\n");

    const int   N_OBJ    = 8000;
    const float HOT_FRAC = 0.10f;
    const int   N_HOT    = (int)(N_OBJ * HOT_FRAC);
    const int   N_EPOCHS = 12;
    const int   SCANS    = 300;

    volatile int runtime_seed = 42;

    HotColdSlab* slab = slab_create(sizeof(Object), N_OBJ + 64);

    SlabHandle* hot_handles  = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)N_HOT);
    SlabHandle* cold_handles = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)(N_OBJ - N_HOT));
    int h_hot = 0, h_cold = 0;

    int hot_stride = (int)(1.0f / HOT_FRAC + 0.5f);
    for (int i = 0; i < N_OBJ; i++) {
        SlabHandle h = slab_alloc(slab);
        Object* obj  = (Object*)slab_get_raw(slab, h);
        obj->id      = i;
        obj->counter = 0;
        obj->value   = (float)(runtime_seed * i + 1) * 0.001f;
        runtime_seed = runtime_seed * 1664525 + 1013904223;

        if (i % hot_stride == 0 && h_hot < N_HOT)
            hot_handles[h_hot++] = h;
        else if (h_cold < N_OBJ - N_HOT)
            cold_handles[h_cold++] = h;
    }

    printf("epoch   compacts   scan(ms)   vs ep0     hot slots\n");

    long volatile sink = 0;
    double t_epoch0 = 0.0;

    for (int epoch = 0; epoch <= N_EPOCHS; epoch++) {
        double t_total = 0.0;
        for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
            double t0 = now_ms();
            for (int sc = 0; sc < SCANS; sc++) {
                for (int i = 0; i < N_HOT; i++) {
                    int slot  = slab->handle_to_slot[hot_handles[i]];
                    Object* o = (Object*)((char*)slab->data +
                                (size_t)slot * sizeof(Object));
                    o->counter++;
                    sink += o->counter;
                }
            }
            double t1 = now_ms();
            if (rep >= WARMUP_REPS) t_total += t1 - t0;
        }
        double t_avg = t_total / MEASURE_REPS;
        if (epoch == 0) t_epoch0 = t_avg;

        double ratio = t_avg / t_epoch0;

        printf("%-7d  %-9d  %-9.3f  %+8.1f%%  %d\n",
               epoch, slab->n_compactions, t_avg,
               (ratio - 1.0) * 100.0, slab->n_hot_slots);

        if (epoch == N_EPOCHS) break;

        for (int iter = 0; iter < SLAB_EPOCH_SIZE; iter++) {
            int     i   = iter % N_HOT;
            Object* o   = (Object*)slab_get(slab, hot_handles[i]);
            o->counter++;
            o->value    = (float)(runtime_seed ^ iter) * 0.001f;
            runtime_seed = runtime_seed * 1664525 + 1013904223;
            sink += (long long)o->value;
        }
    }

    printf("\nhot set: %d objects x %zu bytes = %zu KB\n",
           N_HOT, sizeof(Object), (size_t)N_HOT * sizeof(Object) / 1024);
    printf("packed: %d cache lines  scattered: ~%d cache lines\n\n",
           (int)((size_t)N_HOT * sizeof(Object) + 63) / 64,
           (int)((size_t)N_OBJ  * sizeof(Object) + 63) / 64);

    free(hot_handles);
    free(cold_handles);
    slab_destroy(slab);
    (void)sink;
}


static void exp6_reorder_timing(void)
{
    exp_header("EXP 6: compaction cost per slab size");
    printf("times slab_compact() over N epochs, summarized per slab size\n");
    printf("top 20%% of objects get epoch-size accesses, rest get 1 each\n\n");

    static const struct { int n_obj; int n_epochs; } configs[] = {
        {  1000, 50 },
        {  5000, 30 },
        { 20000, 20 },
    };
    int n_configs = (int)(sizeof(configs) / sizeof(configs[0]));

    const float HOT_FRAC = 0.20f;

    printf("%-10s  %-8s  %-12s  %-12s  %-12s  %s\n",
           "objects", "epochs", "avg (us)", "min (us)", "max (us)", "total (ms)");

    for (int ci = 0; ci < n_configs; ci++) {
        int N_OBJ    = configs[ci].n_obj;
        int N_EPOCHS = configs[ci].n_epochs;
        int N_HOT    = (int)(N_OBJ * HOT_FRAC);
        if (N_HOT < 1) N_HOT = 1;
        int N_COLD   = N_OBJ - N_HOT;

        HotColdSlab* slab = slab_create(sizeof(Object), N_OBJ + 64);

        SlabHandle* hot_handles  = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)N_HOT);
        SlabHandle* cold_handles = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)N_COLD);
        int h_hot = 0, h_cold = 0;

        int hot_stride = (int)(1.0f / HOT_FRAC + 0.5f);
        volatile int seed = 1234;
        for (int i = 0; i < N_OBJ; i++) {
            SlabHandle h = slab_alloc(slab);
            Object* obj  = (Object*)slab_get_raw(slab, h);
            obj->id      = i;
            obj->value   = (float)(seed ^ i) * 0.001f;
            seed = seed * 1664525 + 1013904223;
            if (i % hot_stride == 0 && h_hot < N_HOT)
                hot_handles[h_hot++] = h;
            else if (h_cold < N_COLD)
                cold_handles[h_cold++] = h;
        }

        double sum_us = 0.0;
        double min_us = 1e18;
        double max_us = 0.0;
        long volatile sink = 0;

        for (int epoch = 0; epoch < N_EPOCHS; epoch++) {
            for (int iter = 0; iter < SLAB_EPOCH_SIZE; iter++) {
                int i    = iter % N_HOT;
                int slot = slab->handle_to_slot[hot_handles[i]];
                Object* o = (Object*)slab_get_raw(slab, hot_handles[i]);
                o->counter++;
                slab->meta[slot].access_count++;
                sink += o->counter;
            }
            for (int i = 0; i < N_COLD; i++) {
                int slot = slab->handle_to_slot[cold_handles[i]];
                slab->meta[slot].access_count++;
            }

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            slab_compact(slab);
            clock_gettime(CLOCK_MONOTONIC, &t1);

            double us = (double)(t1.tv_sec  - t0.tv_sec)  * 1e6 +
                        (double)(t1.tv_nsec - t0.tv_nsec) / 1e3;

            sum_us += us;
            if (us < min_us) min_us = us;
            if (us > max_us) max_us = us;

            for (int i = 0; i < slab->capacity; i++)
                if (slab->meta[i].state == SLOT_USED)
                    slab->meta[i].access_count = 0;
            slab->n_epochs++;
        }

        printf("%-10d  %-8d  %-12.2f  %-12.2f  %-12.2f  %.3f\n",
               N_OBJ, N_EPOCHS,
               sum_us / N_EPOCHS, min_us, max_us, sum_us / 1e3);

        free(hot_handles);
        free(cold_handles);
        slab_destroy(slab);
        (void)sink;
    }

    printf("\n");
}


static void exp_epoch_sweep(void)
{
    exp_header("EXP EPOCH SWEEP: effect of epoch size on hot-set scan performance");
    printf("8000 objects, 10%% hot. fixed budget: 80000 accesses on hot set before measuring.\n");
    printf("smaller epoch = more compactions fired within budget = faster convergence.\n\n");

    static const int EPOCH_SIZES[]  = { 2000, 4000, 8000, 16000, 32000 };
    const int   N_EPOCH_SIZES  = 5;
    const int   ACCESS_BUDGET  = 80000;
    const int   N_OBJ          = 8000;
    const float HOT_FRAC       = 0.10f;
    const int   N_HOT          = (int)(N_OBJ * HOT_FRAC);
    const int   N_SCANS        = 200;
    const int   STRIDE         = (int)(1.0f / HOT_FRAC + 0.5f);

    /* baseline: allocate objects but do zero accesses — fully scattered */
    double t_baseline;
    {
        HotColdSlab* slab = slab_create(sizeof(Object), N_OBJ + 64);
        SlabHandle* hot_hs = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)N_HOT);
        int h_hot = 0;
        for (int i = 0; i < N_OBJ; i++) {
            SlabHandle h = slab_alloc(slab);
            ((Object*)slab_get_raw(slab, h))->id = i;
            if (i % STRIDE == 0 && h_hot < N_HOT) hot_hs[h_hot++] = h;
        }
        t_baseline = measure_hot_scan_hc(slab, hot_hs, N_HOT, N_SCANS);
        free(hot_hs);
        slab_destroy(slab);
    }

    printf("%-12s  %-12s  %-14s  %-14s  %s\n",
           "epoch_size", "compactions", "scan_ms", "drive_ms", "speedup_vs_flat");

    for (int ei = 0; ei < N_EPOCH_SIZES; ei++) {
        int epoch_size = EPOCH_SIZES[ei];

        HotColdSlab* slab = slab_create(sizeof(Object), N_OBJ + 64);
        slab->epoch_size  = epoch_size;

        SlabHandle* hot_hs = (SlabHandle*)malloc(sizeof(SlabHandle) * (size_t)N_HOT);
        int h_hot = 0;
        for (int i = 0; i < N_OBJ; i++) {
            SlabHandle h = slab_alloc(slab);
            ((Object*)slab_get_raw(slab, h))->id = i;
            if (i % STRIDE == 0 && h_hot < N_HOT) hot_hs[h_hot++] = h;
        }

        /* drive ACCESS_BUDGET accesses — triggers compactions at every epoch_size interval */
        long volatile sink = 0;
        double t_drive0 = now_ms();
        for (int iter = 0; iter < ACCESS_BUDGET; iter++) {
            Object* o = (Object*)slab_get(slab, hot_hs[iter % N_HOT]);
            o->counter++;
            sink += o->counter;
        }
        double t_drive = now_ms() - t_drive0;

        /* measure steady-state hot-set scan after those accesses */
        double t_scan = measure_hot_scan_hc(slab, hot_hs, N_HOT, N_SCANS);

        printf("%-12d  %-12d  %-14.3f  %-14.3f  %.2fx\n",
               epoch_size, slab->n_compactions, t_scan, t_drive,
               t_baseline / t_scan);

        free(hot_hs);
        slab_destroy(slab);
        (void)sink;
    }

    printf("\nbaseline (no compaction, fully scattered): %.3f ms\n", t_baseline);
    printf("compactions = %d / epoch_size (larger epoch = fewer compactions = less convergence)\n",
           ACCESS_BUDGET);
    printf("drive_ms = total time to run %d accesses, includes compaction overhead\n",
           ACCESS_BUDGET);
    printf("speedup_vs_flat = how much faster scan is vs the uncompacted baseline\n\n");
}


int main(void)
{
    printf("\n");
    printf("*****************************************************************\n");
    printf("*       HOT-COLD SLAB ALLOCATOR -- BENCHMARK                    *\n");
    printf("*****************************************************************\n");
    printf("\n");
    printf("object size    : %zu bytes\n", sizeof(Object));
    printf("objs per line  : %zu\n",       64 / sizeof(Object));
    printf("epoch size     : %d accesses\n", SLAB_EPOCH_SIZE);
    printf("hot percentile : %.0f%%\n\n",  HOT_PERCENTILE * 100);

    exp1_basic_structure();
    exp2_cache_performance();
    exp4_compaction_lifecycle();
    exp5_epoch_performance_progression();
    exp6_reorder_timing();
    exp_epoch_sweep();

    printf("\n=== SUMMARY ===\n\n");
    printf("one flat region, slots sorted by access count every %d accesses\n", SLAB_EPOCH_SIZE);
    printf("hot objects move to the front and share the first cache lines of the slab\n");
    printf("handles stay stable across compactions because handle_to_slot is updated on every swap\n\n");

    return 0;
}
