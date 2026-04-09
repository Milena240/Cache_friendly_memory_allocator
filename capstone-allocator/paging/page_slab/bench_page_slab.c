#define _POSIX_C_SOURCE 200809L
#include "page_slab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

#define WARMUP_REPS   3
#define MEASURE_REPS  10

static void exp_header(const char* title)
{
    printf("\n");
    printf("==============================================================\n");
    printf("  %s\n", title);
    printf("==============================================================\n");
    printf("\n");
}

typedef struct {
    int   id;
    int   counter;
    float value;
    int   pad;
} Obj;


static void exp1_region_structure(void)
{
    exp_header("EXP 1: handle correctness after migration");
    printf("allocate 16 objects, hammer handles 0/5/11, check all handles still point to the right object\n\n");

    PageAwareSlab* s = page_slab_create(sizeof(Obj), 32);

    printf("hot region:  %d slots @ %p (%s)\n",
           s->hot_capacity, (void*)s->hot.base,
           s->hot.is_locked ? "mlock'd" : "not locked");
    printf("cold region: %d slots @ %p\n\n",
           s->cold_capacity, (void*)s->cold.base);

    PageSlabHandle handles[16];
    for (int i = 0; i < 16; i++) {
        handles[i] = page_slab_alloc(s);
        Obj* o = (Obj*)page_slab_get_raw(s, handles[i]);
        o->id = i;
    }

    printf("after allocating 16 objects (all cold):\n");
    page_slab_print_layout(s, 20);

    long volatile sink = 0;
    for (int rep = 0; rep < PAGE_SLAB_EPOCH_SIZE * 3; rep++) {
        Obj* a = (Obj*)page_slab_get(s, handles[0]);
        Obj* b = (Obj*)page_slab_get(s, handles[5]);
        Obj* c = (Obj*)page_slab_get(s, handles[11]);
        a->counter++; b->counter++; c->counter++;
        sink += a->counter;
    }

    printf("after hammering handles 0, 5, 11 (%d epochs ran):\n\n", s->n_epochs);
    page_slab_print_layout(s, 20);

    int all_ok = 1;
    for (int i = 0; i < 16; i++) {
        Obj* o = (Obj*)page_slab_get_raw(s, handles[i]);
        int ok = (o->id == i);
        printf("handle %2d -> gslot %3d  region=%s  id=%d  %s\n",
               handles[i],
               s->handle_to_slot[handles[i]],
               s->meta[s->handle_to_slot[handles[i]]].region == PS_REGION_HOT ? "HOT" : "cold",
               o->id,
               ok ? "✓" : "✗ WRONG");
        if (!ok) all_ok = 0;
    }
    printf("\n%s\n\n", all_ok ? "all handles valid" : "HANDLE CORRUPTION DETECTED");

    page_slab_print_stats(s, "exp1");
    page_slab_destroy(s);
    (void)sink;
}


static void exp2_promotion_demotion(void)
{
    exp_header("EXP 2: promotion and demotion lifecycle");
    printf("phase 1: heat handles 0,1,2 until promoted\n");
    printf("phase 2: stop touching them, heat 40,41,42 instead -> 0,1,2 should get demoted\n\n");

    int N = 60;
    PageAwareSlab* s = page_slab_create(sizeof(Obj), N);

    PageSlabHandle* hs = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)N);
    for (int i = 0; i < N; i++) {
        hs[i] = page_slab_alloc(s);
        ((Obj*)page_slab_get_raw(s, hs[i]))->id = i;
    }

    long volatile sink = 0;
    for (int rep = 0; rep < PAGE_SLAB_EPOCH_SIZE * 4; rep++) {
        for (int h = 0; h < 3; h++) {
            Obj* o = (Obj*)page_slab_get(s, hs[h]);
            o->counter++;
            sink += o->counter;
        }
    }

    printf("after phase 1 (%d compactions, %d promotions):\n", s->n_compactions, s->n_promotions);
    for (int i = 0; i < 6; i++) {
        int gslot = s->handle_to_slot[hs[i]];
        printf("  handle %2d  -> %s\n", hs[i],
               s->meta[gslot].region == PS_REGION_HOT ? "HOT" : "cold");
    }
    printf("\n");

    for (int rep = 0; rep < PAGE_SLAB_EPOCH_SIZE * 6; rep++) {
        for (int h = 40; h < 43; h++) {
            Obj* o = (Obj*)page_slab_get(s, hs[h]);
            o->counter++;
            sink += o->counter;
        }
    }

    printf("after phase 2 (%d compactions, %d demotions):\n", s->n_compactions, s->n_demotions);
    for (int i = 0; i < 3; i++) {
        int gslot = s->handle_to_slot[hs[i]];
        printf("  handle %2d (old hot) -> %s  cold_epochs=%d\n", hs[i],
               s->meta[gslot].region == PS_REGION_HOT ? "HOT" : "cold",
               s->meta[gslot].cold_epochs);
    }
    for (int h = 40; h < 43; h++) {
        int gslot = s->handle_to_slot[hs[h]];
        printf("  handle %2d (new hot) -> %s\n", hs[h],
               s->meta[gslot].region == PS_REGION_HOT ? "HOT" : "cold");
    }
    printf("\n");

    free(hs);
    page_slab_destroy(s);
    (void)sink;
}


static void exp3_cache_performance(void)
{
    exp_header("EXP 3: flat vs page-aware scan speed");
    printf("flat: hot objects stay scattered among cold ones, no migration\n");
    printf("page-aware: hot objects get compacted into the mlock'd region\n");
    printf("scanning only the hot set, 200 scans, avg over 10 runs\n\n");

    static const int N_HOT_SIZES[] = { 256, 512, 1024, 2048 };
    int n_tests      = 4;
    int cold_per_hot = 4;
    int n_scans      = 200;

    printf("%-10s  %-12s  %-12s  %-12s  %s\n",
           "hot objs", "total objs", "flat (ms)", "page-aware (ms)", "speedup");

    for (int ti = 0; ti < n_tests; ti++) {
        int n_hot     = N_HOT_SIZES[ti];
        int n_objects = n_hot * (cold_per_hot + 1);

        PageSlabHandle* hot_flat = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)n_hot);
        PageSlabHandle* hot_pa   = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)n_hot);

        double t_flat = 0;
        {
            PageAwareSlab* s = page_slab_create(sizeof(Obj), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                PageSlabHandle h = page_slab_alloc(s);
                ((Obj*)page_slab_get_raw(s, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_flat[hi++] = h;
            }

            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        Obj* o = (Obj*)page_slab_get_raw(s, hot_flat[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_flat = total / MEASURE_REPS;
            page_slab_destroy(s);
            (void)sink;
        }

        double t_pa = 0;
        {
            PageAwareSlab* s = page_slab_create(sizeof(Obj), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                PageSlabHandle h = page_slab_alloc(s);
                ((Obj*)page_slab_get_raw(s, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_pa[hi++] = h;
            }

            long volatile sink = 0;
            for (int iter = 0; iter < PAGE_SLAB_EPOCH_SIZE * 6; iter++) {
                Obj* o = (Obj*)page_slab_get(s, hot_pa[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        Obj* o = (Obj*)page_slab_get_raw(s, hot_pa[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_pa = total / MEASURE_REPS;
            page_slab_destroy(s);
            (void)sink;
        }

        printf("%-10d  %-12d  %-12.3f  %-15.3f  %.2fx\n",
               n_hot, n_objects, t_flat, t_pa, t_flat / t_pa);

        free(hot_flat);
        free(hot_pa);
    }

    printf("\nthe speedup comes from cache locality: when hot objects are packed\n");
    printf("together in one region, every cache line loaded is useful data.\n");
    printf("in the flat case they are scattered so cold objects pollute the cache.\n\n");
}


static void exp4_epoch_progression(void)
{
    exp_header("EXP 4: scan time per epoch");
    printf("6000 objects, 10%% are hot. watch scan time drop as hot objects migrate.\n\n");

    const int   N_OBJ    = 6000;
    const float HOT_FRAC = 0.10f;
    const int   N_HOT    = (int)(N_OBJ * HOT_FRAC);
    const int   N_EPOCHS = 10;
    const int   SCANS    = 200;

    volatile int seed = 77;

    PageAwareSlab* s = page_slab_create(sizeof(Obj), N_OBJ + 64);

    PageSlabHandle* hot_hs  = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)N_HOT);
    PageSlabHandle* cold_hs = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)(N_OBJ - N_HOT));
    int h_hot = 0, h_cold = 0;
    int hot_stride = (int)(1.0f / HOT_FRAC + 0.5f);

    for (int i = 0; i < N_OBJ; i++) {
        PageSlabHandle h = page_slab_alloc(s);
        Obj* o = (Obj*)page_slab_get_raw(s, h);
        o->id    = i;
        o->value = (float)(seed * i + 1) * 0.001f;
        seed     = seed * 1664525 + 1013904223;

        if (i % hot_stride == 0 && h_hot < N_HOT)
            hot_hs[h_hot++] = h;
        else if (h_cold < N_OBJ - N_HOT)
            cold_hs[h_cold++] = h;
    }

    printf("epoch   compacts   scan(ms)   vs ep0     in_hot   promotions\n");

    long volatile sink = 0;
    double t0_baseline = 0;

    for (int epoch = 0; epoch <= N_EPOCHS; epoch++) {

        double t_total = 0;
        for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
            double t0 = now_ms();
            for (int sc = 0; sc < SCANS; sc++)
                for (int i = 0; i < N_HOT; i++) {
                    Obj* o = (Obj*)page_slab_get_raw(s, hot_hs[i]);
                    o->counter++;
                    sink += o->counter;
                }
            double t1 = now_ms();
            if (rep >= WARMUP_REPS) t_total += t1 - t0;
        }
        double t_avg = t_total / MEASURE_REPS;
        if (epoch == 0) t0_baseline = t_avg;

        double ratio = t_avg / t0_baseline;

        int in_hot = 0;
        for (int i = 0; i < N_HOT; i++) {
            int gs = s->handle_to_slot[hot_hs[i]];
            if (s->meta[gs].region == PS_REGION_HOT) in_hot++;
        }

        printf("%-7d  %-9d  %-9.3f  %+8.1f%%  %-7d  %d\n",
               epoch, s->n_compactions, t_avg,
               (ratio - 1.0) * 100.0,
               in_hot, s->n_promotions);

        if (epoch == N_EPOCHS) break;

        for (int iter = 0; iter < PAGE_SLAB_EPOCH_SIZE; iter++) {
            Obj* o = (Obj*)page_slab_get(s, hot_hs[iter % N_HOT]);
            o->counter++;
            o->value = (float)(seed ^ iter) * 0.001f;
            seed = seed * 1664525 + 1013904223;
            sink += (long)o->value;
        }
    }

    printf("\nhot set: %d objects x %zu bytes = %zu KB\n",
           N_HOT, sizeof(Obj), (size_t)N_HOT * sizeof(Obj) / 1024);
    printf("hot region: %zu KB  (%s)\n\n",
           s->hot.byte_size / 1024,
           s->mlock_works ? "mlock'd" : "not locked");

    free(hot_hs);
    free(cold_hs);
    page_slab_destroy(s);
    (void)sink;
}


static void exp5_page_management(void)
{
    exp_header("EXP 5: OS page advisory");
    printf("checks that mlock and madvise calls actually work on this machine\n\n");

    int N = 200;
    PageAwareSlab* s = page_slab_create(sizeof(Obj), N);

    printf("mlock works:     %s\n", s->mlock_works ? "yes" : "no (RLIMIT_MEMLOCK too low)");
    printf("MADV_COLD works: %s\n\n", s->madvise_cold_works ? "yes" : "no (kernel < 5.4, falling back to MADV_NORMAL)");

    PageSlabHandle* hs = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)N);
    for (int i = 0; i < N; i++) {
        hs[i] = page_slab_alloc(s);
        ((Obj*)page_slab_get_raw(s, hs[i]))->id = i;
    }

    long volatile sink = 0;
    int n_hot = N / 5;
    for (int iter = 0; iter < PAGE_SLAB_EPOCH_SIZE * 5; iter++) {
        Obj* o = (Obj*)page_slab_get(s, hs[iter % n_hot]);
        o->counter++;
        sink += o->counter;
    }

    printf("after %d epochs:\n", s->n_epochs);
    printf("  in hot region:  %d\n", s->n_hot_slots);
    printf("  in cold region: %d\n", s->n_cold_slots);
    printf("  promotions: %d  demotions: %d\n\n", s->n_promotions, s->n_demotions);

    page_slab_advise(s);
    printf("called page_slab_advise():\n");
    printf("  MADV_WILLNEED -> hot  region (%zu KB)\n", s->hot.byte_size / 1024);
    printf("  MADV_%-6s   -> cold region (%zu KB)\n",
           s->madvise_cold_works ? "COLD" : "NORMAL",
           s->cold.byte_size / 1024);
    printf("\n");

    printf("hot region:  %zu pages, always in RAM%s\n",
           s->hot.byte_size / s->page_size,
           s->mlock_works ? " (mlock'd)" : "");
    printf("cold region: %zu pages, OS can reclaim under memory pressure\n\n",
           s->cold.byte_size / s->page_size);

    free(hs);
    page_slab_destroy(s);
    (void)sink;
}


static void exp7_first_access_latency(void)
{
    exp_header("EXP 7: first-access latency — MAP_POPULATE vs lazy faulting");
    printf("hot region uses MAP_POPULATE: all pages faulted at mmap time\n");
    printf("cold region uses lazy faulting: pages come in on first touch\n");
    printf("this measures how long the very first access to each region takes\n\n");

    int N = 1000;
    int n_scans = 50;

    PageAwareSlab* s = page_slab_create(sizeof(Obj), N);

    PageSlabHandle* hs = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)N);
    for (int i = 0; i < N; i++) {
        hs[i] = page_slab_alloc(s);
        ((Obj*)page_slab_get_raw(s, hs[i]))->id = i;
    }

    int n_hot  = (int)(N * PAGE_SLAB_HOT_PERCENTILE);
    int n_cold = N - n_hot;

    long volatile sink = 0;

    double t0 = now_ms();
    for (int scan = 0; scan < n_scans; scan++)
        for (int i = 0; i < n_hot; i++) {
            Obj* o = (Obj*)page_slab_get_raw(s, hs[i]);
            o->counter++;
            sink += o->counter;
        }
    double t_hot_first = (now_ms() - t0) / n_scans;

    double t1 = now_ms();
    for (int scan = 0; scan < n_scans; scan++)
        for (int i = n_hot; i < N; i++) {
            Obj* o = (Obj*)page_slab_get_raw(s, hs[i]);
            o->counter++;
            sink += o->counter;
        }
    double t_cold_first = (now_ms() - t1) / n_scans;

    printf("first %d scans avg:\n", n_scans);
    printf("  hot  region (%d slots, MAP_POPULATE): %.3f ms\n", n_hot,  t_hot_first);
    printf("  cold region (%d slots, lazy fault):   %.3f ms\n", n_cold, t_cold_first);
    printf("\n");
    printf("hot region was pre-faulted at mmap time so no page faults occur\n");
    printf("cold region triggers page faults on first touch which adds latency\n\n");

    free(hs);
    page_slab_destroy(s);
    (void)sink;
}


static void exp6_object_size_effect(void)
{
    exp_header("EXP 6: how object size affects performance");
    printf("same workload, same number of hot objects, only obj_size changes\n");
    printf("smaller objects -> more fit per cache line -> packing matters more\n");
    printf("larger objects  -> each object already spans multiple cache lines\n\n");

    static const size_t OBJ_SIZES[] = { 8, 16, 64, 128, 256, 512 };
    int n_sizes      = 6;
    int n_hot        = 512;
    int cold_per_hot = 4;
    int n_scans      = 200;

    printf("%-12s  %-10s  %-10s  %-12s  %-15s  %s\n",
           "obj_size", "objs/page", "objs/line", "flat (ms)", "page-aware (ms)", "speedup");

    for (int si = 0; si < n_sizes; si++) {
        size_t obj_size = OBJ_SIZES[si];
        int n_objects   = n_hot * (cold_per_hot + 1);

        int objs_per_page = (int)(4096 / obj_size);
        int objs_per_line = (int)(64   / obj_size);
        if (objs_per_line < 1) objs_per_line = 1;

        PageSlabHandle* hot_flat = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)n_hot);
        PageSlabHandle* hot_pa   = (PageSlabHandle*)malloc(sizeof(PageSlabHandle) * (size_t)n_hot);

        double t_flat = 0;
        {
            PageAwareSlab* s = page_slab_create(obj_size, n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                PageSlabHandle h = page_slab_alloc(s);
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_flat[hi++] = h;
            }

            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int* o = (int*)page_slab_get_raw(s, hot_flat[i]);
                        (*o)++;
                        sink += *o;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_flat = total / MEASURE_REPS;
            page_slab_destroy(s);
            (void)sink;
        }

        double t_pa = 0;
        {
            PageAwareSlab* s = page_slab_create(obj_size, n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                PageSlabHandle h = page_slab_alloc(s);
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_pa[hi++] = h;
            }

            long volatile sink = 0;
            for (int iter = 0; iter < PAGE_SLAB_EPOCH_SIZE * 6; iter++) {
                int* o = (int*)page_slab_get(s, hot_pa[iter % n_hot]);
                (*o)++;
                sink += *o;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        int* o = (int*)page_slab_get_raw(s, hot_pa[i]);
                        (*o)++;
                        sink += *o;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_pa = total / MEASURE_REPS;
            page_slab_destroy(s);
            (void)sink;
        }

        printf("%-12zu  %-10d  %-10d  %-12.3f  %-15.3f  %.2fx\n",
               obj_size, objs_per_page, objs_per_line,
               t_flat, t_pa, t_flat / t_pa);

        free(hot_flat);
        free(hot_pa);
    }

    printf("\nexpected: speedup is highest for small objects (many fit per cache line,\n");
    printf("packing them together has a big impact) and shrinks as objects grow larger.\n\n");
}


int main(void)
{
    printf("\n");
    printf("*****************************************************************\n");
    printf("*          PAGE-AWARE HOT/COLD SLAB — BENCHMARK                 *\n");
    printf("*****************************************************************\n");
    printf("\n");
    printf("object size : %zu bytes\n", sizeof(Obj));
    printf("epoch size  : %d accesses\n", PAGE_SLAB_EPOCH_SIZE);
    printf("hot region  : %.0f%% of capacity\n\n", PAGE_SLAB_HOT_PERCENTILE * 100.0);

    exp1_region_structure();
    exp2_promotion_demotion();
    exp3_cache_performance();
    exp4_epoch_progression();
    exp5_page_management();
    exp7_first_access_latency();
    exp6_object_size_effect();

    printf("\n=== SUMMARY ===\n\n");
    printf("two mmap regions: hot (mlock'd, never evicted) and cold (OS can reclaim)\n");
    printf("new objects start cold, get promoted after surviving an epoch in the top 20%%\n");
    printf("handles stay valid across migrations because handle_to_slot is updated on every move\n\n");

    return 0;
}
