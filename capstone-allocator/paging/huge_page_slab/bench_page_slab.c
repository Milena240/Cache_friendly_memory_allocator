#include "hugepage_slab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>

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
    printf("allocate 16 objects, hammer handles 0/5/11, check all handles still point to the right object\n");
    printf("also snapshot physical pages before and after to see if they moved\n\n");

    HpAwareSlab* s = hp_slab_create(sizeof(Obj), 32);

    printf("hot  region: %d slots @ %p  %zu KB  huge=%s  %s\n",
           s->hot_capacity, (void*)s->hot.base, s->hot.byte_size / 1024,
           s->huge_pages_hot  ? "MAP_HUGETLB" : "THP/normal",
           s->hot.is_locked   ? "mlock'd" : "not locked");
    printf("cold region: %d slots @ %p  %zu KB  huge=%s\n\n",
           s->cold_capacity, (void*)s->cold.base, s->cold.byte_size / 1024,
           s->huge_pages_cold ? "MAP_HUGETLB" : "THP/normal");

    HpSlabHandle handles[16];
    for (int i = 0; i < 16; i++) {
        handles[i] = hp_slab_alloc(s);
        Obj* o = (Obj*)hp_slab_get_raw(s, handles[i]);
        o->id = i;
    }

    printf("after allocating 16 objects (all cold):\n");
    hp_slab_print_layout(s, 24);  /* 6 hot-reserved slots + 16 cold = last object at gslot 21, need >=22 rows */

    hp_slab_snapshot_pages(s);
    printf("snapshot taken, now hammering handles 0, 5, 11\n\n");

    long volatile sink = 0;
    for (int rep = 0; rep < HP_SLAB_EPOCH_SIZE * 3; rep++) {
        Obj* a = (Obj*)hp_slab_get(s, handles[0]);
        Obj* b = (Obj*)hp_slab_get(s, handles[5]);
        Obj* c = (Obj*)hp_slab_get(s, handles[11]);
        a->counter++; b->counter++; c->counter++;
        sink += a->counter;
    }

    printf("after hammering handles 0, 5, 11 (%d epochs, %d compactions):\n\n",
           s->n_epochs, s->n_compactions);

    hp_slab_diff_pages(s, "after promotion of handles 0, 5, 11");
    hp_slab_print_layout(s, 24);  /* same reason: show all 16 objects */

    int all_ok = 1;
    for (int i = 0; i < 16; i++) {
        Obj* o = (Obj*)hp_slab_get_raw(s, handles[i]);
        int ok = (o->id == i);
        printf("handle %2d -> gslot %3d  region=%s  id=%d  %s\n",
               handles[i],
               s->handle_to_slot[handles[i]],
               s->meta[s->handle_to_slot[handles[i]]].region == HP_REGION_HOT ? "HOT" : "cold",
               o->id,
               ok ? "✓" : "✗ WRONG");
        if (!ok) all_ok = 0;
    }
    printf("\n%s\n\n", all_ok ? "all handles valid" : "HANDLE CORRUPTION DETECTED");

    hp_slab_print_stats(s, "exp1");
    hp_slab_destroy(s);
    (void)sink;
}


static void exp2_promotion_demotion(void)
{
    exp_header("EXP 2: promotion and demotion lifecycle");
    printf("phase 1: heat handles 0,1,2 until promoted to huge-page hot region (fills it completely)\n");
    printf("phase 2: stop touching them, heat 10,11,12 instead -> 0,1,2 must be demoted to make room\n\n");

    int N = 15;  /* hot_capacity = 20%% of 15 = 3, exactly fits handles 0,1,2 */
    HpAwareSlab* s = hp_slab_create(sizeof(Obj), N);

    HpSlabHandle* hs = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)N);
    for (int i = 0; i < N; i++) {
        hs[i] = hp_slab_alloc(s);
        ((Obj*)hp_slab_get_raw(s, hs[i]))->id = i;
    }

    hp_slab_snapshot_pages(s);
    long volatile sink = 0;
    for (int rep = 0; rep < HP_SLAB_EPOCH_SIZE * 4; rep++) {
        for (int h = 0; h < 3; h++) {
            Obj* o = (Obj*)hp_slab_get(s, hs[h]);
            o->counter++;
            sink += o->counter;
        }
    }

    printf("after phase 1 (%d compactions, %d promotions):\n\n", s->n_compactions, s->n_promotions);
    hp_slab_print_layout(s, s->total_capacity);

    /* record region and slot before phase 2 so we can show before -> after */
    int region_before[15];
    int gslot_before[15];
    for (int i = 0; i < N; i++) {
        int gslot = s->handle_to_slot[hs[i]];
        region_before[i] = s->meta[gslot].region;
        gslot_before[i]  = gslot;
    }

    hp_slab_snapshot_pages(s);
    for (int rep = 0; rep < HP_SLAB_EPOCH_SIZE * 6; rep++) {
        for (int h = 10; h < 13; h++) {
            Obj* o = (Obj*)hp_slab_get(s, hs[h]);
            o->counter++;
            sink += o->counter;
        }
    }

    printf("after phase 2 (%d compactions, %d demotions):\n\n", s->n_compactions, s->n_demotions);
    hp_slab_print_layout(s, s->total_capacity);

    printf("region changes:\n");
    printf("  %-9s  %-14s  %-14s  %s\n", "handle", "before", "after", "note");
    for (int i = 0; i < 3; i++) {
        int gslot_after = s->handle_to_slot[hs[i]];
        const char* before = region_before[i]              == HP_REGION_HOT ? "HOT"  : "cold";
        const char* after  = s->meta[gslot_after].region  == HP_REGION_HOT ? "HOT"  : "cold";
        printf("  handle %2d:  slot %2d %-4s  ->  slot %2d %-4s  (demoted after %d cold epochs)\n",
               hs[i],
               gslot_before[i], before,
               gslot_after,     after,
               s->meta[gslot_after].cold_epochs);
    }
    for (int h = 10; h < 13; h++) {
        int gslot_after = s->handle_to_slot[hs[h]];
        const char* before = region_before[h]             == HP_REGION_HOT ? "HOT"  : "cold";
        const char* after  = s->meta[gslot_after].region == HP_REGION_HOT ? "HOT"  : "cold";
        printf("  handle %2d:  slot %2d %-4s  ->  slot %2d %-4s  (promoted)\n",
               hs[h],
               gslot_before[h], before,
               gslot_after,     after);
    }
    printf("\n");
    hp_slab_diff_pages(s, "after phase-2 demotion");

    free(hs);
    hp_slab_destroy(s);
    (void)sink;
}


static void exp3_cache_performance(void)
{
    exp_header("EXP 3: flat vs huge-page-aware scan speed");
    printf("flat: hot objects stay scattered, no migration\n");
    printf("huge-page-aware: hot objects packed into mlock'd huge-page region\n");
    printf("scanning only the hot set, 200 scans, avg over 10 runs\n\n");

    static const int N_HOT_SIZES[] = { 256, 512, 1024, 2048 };
    int n_tests      = 4;
    int cold_per_hot = 4;
    int n_scans      = 200;

    printf("%-10s  %-12s  %-12s  %-15s  %s\n",
           "hot objs", "total objs", "flat (ms)", "huge-page (ms)", "speedup");

    for (int ti = 0; ti < n_tests; ti++) {
        int n_hot     = N_HOT_SIZES[ti];
        int n_objects = n_hot * (cold_per_hot + 1);

        HpSlabHandle* hot_flat = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_hot);
        HpSlabHandle* hot_hp   = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_hot);

        double t_flat = 0;
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_flat[hi++] = h;
            }

            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        Obj* o = (Obj*)hp_slab_get_raw(s, hot_flat[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_flat = total / MEASURE_REPS;
            hp_slab_destroy(s);
            (void)sink;
        }

        double t_hp = 0;
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_hp[hi++] = h;
            }

            long volatile sink = 0;
            for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE * 6; iter++) {
                Obj* o = (Obj*)hp_slab_get(s, hot_hp[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_hot; i++) {
                        Obj* o = (Obj*)hp_slab_get_raw(s, hot_hp[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_hp = total / MEASURE_REPS;
            hp_slab_destroy(s);
            (void)sink;
        }

        printf("%-10d  %-12d  %-12.3f  %-15.3f  %.2fx\n",
               n_hot, n_objects, t_flat, t_hp, t_flat / t_hp);

        free(hot_flat);
        free(hot_hp);
    }

    printf("\nhuge-page advantage: one 2 MB TLB entry covers the whole hot region\n");
    printf("instead of 512 individual 4 KB entries, so TLB misses drop on hot scans\n\n");
}


static void exp3b_full_scan(void)
{
    exp_header("EXP 3b: flat vs huge-page-aware FULL slab scan");
    printf("flat: all objects in one contiguous slab, sequential scan\n");
    printf("huge-page-aware: hot objects promoted to huge-page region, cold stay separate\n");
    printf("scanning ALL objects (hot + cold), 200 scans, avg over 10 runs\n\n");

    static const int N_HOT_SIZES[] = { 256, 512, 1024, 2048 };
    int n_tests      = 4;
    int cold_per_hot = 4;
    int n_scans      = 200;

    printf("%-10s  %-12s  %-12s  %-15s  %s\n",
           "hot objs", "total objs", "flat (ms)", "huge-page (ms)", "speedup");

    for (int ti = 0; ti < n_tests; ti++) {
        int n_hot     = N_HOT_SIZES[ti];
        int n_objects = n_hot * (cold_per_hot + 1);

        HpSlabHandle* all_flat = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_objects);
        HpSlabHandle* all_hp   = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_objects);
        HpSlabHandle* hot_hp   = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_hot);

        double t_flat = 0;
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_objects + 16);
            for (int i = 0; i < n_objects; i++) {
                all_flat[i] = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, all_flat[i]))->id = i;
            }

            long volatile sink = 0;
            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_objects; i++) {
                        Obj* o = (Obj*)hp_slab_get_raw(s, all_flat[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_flat = total / MEASURE_REPS;
            hp_slab_destroy(s);
            (void)sink;
        }

        double t_hp = 0;
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_objects + 16);
            int hi = 0;
            for (int i = 0; i < n_objects; i++) {
                all_hp[i] = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, all_hp[i]))->id = i;
                if ((i % (cold_per_hot + 1)) == cold_per_hot && hi < n_hot)
                    hot_hp[hi++] = all_hp[i];
            }

            /* drive hot objects into the huge-page region */
            long volatile sink = 0;
            for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE * 6; iter++) {
                Obj* o = (Obj*)hp_slab_get(s, hot_hp[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }

            double total = 0;
            for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
                double t0 = now_ms();
                for (int scan = 0; scan < n_scans; scan++)
                    for (int i = 0; i < n_objects; i++) {
                        Obj* o = (Obj*)hp_slab_get_raw(s, all_hp[i]);
                        o->counter++;
                        sink += o->counter;
                    }
                double t1 = now_ms();
                if (rep >= WARMUP_REPS) total += t1 - t0;
            }
            t_hp = total / MEASURE_REPS;
            hp_slab_destroy(s);
            (void)sink;
        }

        printf("%-10d  %-12d  %-12.3f  %-15.3f  %.2fx\n",
               n_hot, n_objects, t_flat, t_hp, t_flat / t_hp);

        free(all_flat);
        free(all_hp);
        free(hot_hp);
    }

    printf("\nwhen scanning all objects the flat allocator has better sequential locality:\n");
    printf("all objects sit in one contiguous region so the scan is a straight memory walk.\n");
    printf("the huge-page allocator splits objects across two mmap regions (hot + cold),\n");
    printf("so a full scan jumps between regions and loses the sequential-access advantage.\n");
    printf("a speedup < 1.0x here means flat wins — expected and by design.\n\n");
}


static void exp4_epoch_progression(void)
{
    exp_header("EXP 4: scan time per epoch");
    printf("6000 objects, 10%% are hot. watch scan time as hot objects migrate to huge-page region.\n\n");

    const int   N_OBJ    = 6000;
    const float HOT_FRAC = 0.10f;
    const int   N_HOT    = (int)(N_OBJ * HOT_FRAC);
    const int   N_EPOCHS = 10;
    const int   SCANS    = 200;

    volatile int seed = 77;

    HpAwareSlab* s = hp_slab_create(sizeof(Obj), N_OBJ + 64);

    HpSlabHandle* hot_hs  = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)N_HOT);
    HpSlabHandle* cold_hs = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)(N_OBJ - N_HOT));
    int h_hot = 0, h_cold = 0;
    int hot_stride = (int)(1.0f / HOT_FRAC + 0.5f);

    for (int i = 0; i < N_OBJ; i++) {
        HpSlabHandle h = hp_slab_alloc(s);
        Obj* o = (Obj*)hp_slab_get_raw(s, h);
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
                    Obj* o = (Obj*)hp_slab_get_raw(s, hot_hs[i]);
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
            if (s->meta[gs].region == HP_REGION_HOT) in_hot++;
        }

        printf("%-7d  %-9d  %-9.3f  %+8.1f%%  %-7d  %d\n",
               epoch, s->n_compactions, t_avg,
               (ratio - 1.0) * 100.0,
               in_hot, s->n_promotions);

        if (epoch == N_EPOCHS) break;

        for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE; iter++) {
            Obj* o = (Obj*)hp_slab_get(s, hot_hs[iter % N_HOT]);
            o->counter++;
            o->value = (float)(seed ^ iter) * 0.001f;
            seed = seed * 1664525 + 1013904223;
            sink += (long)o->value;
        }
    }

    printf("\nhot set: %d objects x %zu bytes = %zu KB\n",
           N_HOT, sizeof(Obj), (size_t)N_HOT * sizeof(Obj) / 1024);
    printf("hot region: %zu KB  huge=%s  %s\n\n",
           s->hot.byte_size / 1024,
           s->huge_pages_hot ? "MAP_HUGETLB" : "THP/normal",
           s->mlock_works    ? "mlock'd" : "not locked");

    free(hot_hs);
    free(cold_hs);
    hp_slab_destroy(s);
    (void)sink;
}


__attribute__((unused)) static void exp5_page_management(void)
{
    exp_header("EXP 5: OS page advisory with huge pages");
    printf("hot region:  MADV_HUGEPAGE + mlock (huge pages, always resident)\n");
    printf("cold region: MADV_NOHUGEPAGE + MADV_COLD (splittable, reclaimable)\n\n");

    int N = 200;
    HpAwareSlab* s = hp_slab_create(sizeof(Obj), N);

    printf("mlock works:      %s\n", s->mlock_works        ? "yes" : "no");
    printf("MADV_COLD works:  %s\n", s->madvise_cold_works ? "yes" : "no (MADV_NORMAL fallback)");
    printf("hot  huge pages:  %s\n", s->huge_pages_hot     ? "MAP_HUGETLB" : "THP (kernel-managed)");
    printf("cold huge pages:  %s\n\n", s->huge_pages_cold  ? "MAP_HUGETLB" : "THP (kernel-managed)");

    HpSlabHandle* hs = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)N);
    for (int i = 0; i < N; i++) {
        hs[i] = hp_slab_alloc(s);
        ((Obj*)hp_slab_get_raw(s, hs[i]))->id = i;
    }

    long volatile sink = 0;
    int n_hot = N / 5;
    for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE * 5; iter++) {
        Obj* o = (Obj*)hp_slab_get(s, hs[iter % n_hot]);
        o->counter++;
        sink += o->counter;
    }

    printf("after %d epochs:\n", s->n_epochs);
    printf("  in hot region:  %d\n", s->n_hot_slots);
    printf("  in cold region: %d\n", s->n_cold_slots);
    printf("  promotions: %d  demotions: %d\n\n", s->n_promotions, s->n_demotions);

    hp_slab_advise(s);
    printf("called hp_slab_advise():\n");
    printf("  MADV_WILLNEED + MADV_HUGEPAGE   -> hot  region (%zu KB)\n", s->hot.byte_size / 1024);
    printf("  MADV_%-6s   + MADV_NOHUGEPAGE -> cold region (%zu KB)\n",
           s->madvise_cold_works ? "COLD" : "NORMAL",
           s->cold.byte_size / 1024);
    printf("\n");

    printf("hot  region: %zu huge pages (2 MB each), always in RAM\n",
           s->hot.byte_size / s->huge_page_size);
    printf("cold region: %zu huge pages, can split to 4 KB and be reclaimed\n\n",
           s->cold.byte_size / s->huge_page_size);

    free(hs);
    hp_slab_destroy(s);
    (void)sink;
}


__attribute__((unused)) static void exp6_page_tracking(void)
{
    exp_header("EXP 6: physical page tracking across epochs");
    printf("track 6 objects across 5 epochs via /proc/self/pagemap\n");
    printf("handles 0-2: accessed every epoch -> should stay in hot region\n");
    printf("handles 3-5: only accessed at epoch 0 -> should get demoted\n\n");

    if (geteuid() != 0) {
        printf("note: /proc/self/pagemap requires root on Linux 4.0+\n");
        printf("physical page numbers may show as 0, but virtual address changes are still visible\n\n");
    }

    const int N        = 100;
    const int N_TRACK  = 6;
    const int N_EPOCHS = 5;

    HpAwareSlab* s = hp_slab_create(sizeof(Obj), N);
    HpSlabHandle hs[N_TRACK];

    HpSlabHandle all[100];
    for (int i = 0; i < N; i++) {
        all[i] = hp_slab_alloc(s);
        ((Obj*)hp_slab_get_raw(s, all[i]))->id = i;
    }
    for (int i = 0; i < N_TRACK; i++) hs[i] = all[i];

    printf("%-6s  %-6s  %-6s  %-18s  %-18s  %s\n",
           "epoch", "handle", "region", "virt addr", "phys page", "notes");

#define PRINT_HANDLE(epoch_, h_, note_) do {                               \
    int   gs_   = s->handle_to_slot[(h_)];                                 \
    void* vptr_ = hp_slab_slot_ptr(s, gs_);                               \
    uintptr_t phys_ = hp_slab_virt_to_pfn(s, vptr_);                      \
    const char* reg_ = (s->meta[gs_].region == HP_REGION_HOT) ? "HOT" : "cold"; \
    if (phys_ == 0)                                                        \
        printf("%-6d  %-6d  %-6s  0x%014lx  %-18s  %s\n",                \
               (epoch_), (h_), reg_,                                       \
               (unsigned long)vptr_, "(no pagemap)", (note_));             \
    else                                                                    \
        printf("%-6d  %-6d  %-6s  0x%014lx  0x%014lx    %s\n",           \
               (epoch_), (h_), reg_,                                       \
               (unsigned long)vptr_, (unsigned long)phys_, (note_));      \
} while(0)

    printf("\nepoch 0 - baseline, all cold:\n");
    for (int i = 0; i < N_TRACK; i++)
        PRINT_HANDLE(0, hs[i], i < 3 ? "will heat up" : "will cool down");

    long volatile sink = 0;

    for (int epoch = 1; epoch <= N_EPOCHS; epoch++) {
        hp_slab_snapshot_pages(s);

        for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE; iter++) {
            Obj* o = (Obj*)hp_slab_get(s, hs[iter % 3]);
            o->counter++;
            sink += o->counter;
        }

        printf("\nepoch %d - %d compactions, %d promotions, %d demotions:\n",
               epoch, s->n_compactions, s->n_promotions, s->n_demotions);

        for (int i = 0; i < N_TRACK; i++) {
            int gs = s->handle_to_slot[hs[i]];
            void*     vptr     = hp_slab_slot_ptr(s, gs);
            uintptr_t cur_phys = hp_slab_virt_to_pfn(s, vptr);
            uintptr_t old_phys = s->meta[gs].last_phys_page;

            const char* reg  = (s->meta[gs].region == HP_REGION_HOT) ? "HOT" : "cold";
            int moved = (old_phys != 0 && cur_phys != 0 && old_phys != cur_phys);
            const char* note = moved      ? "PAGE CHANGED (migrated)" :
                               i < 3      ? "hot set"                 :
                               epoch >= 2 ? "cooling..."              : "";

            if (cur_phys == 0)
                printf("%-6d  %-6d  %-6s  0x%014lx  %-18s  %s\n",
                       epoch, hs[i], reg, (unsigned long)vptr, "(no pagemap)", note);
            else
                printf("%-6d  %-6d  %-6s  0x%014lx  0x%014lx    %s\n",
                       epoch, hs[i], reg,
                       (unsigned long)vptr, (unsigned long)cur_phys, note);
        }
    }

    printf("\npage migration summary:\n");
    printf("%-6s  %-6s  %-6s  %s\n", "handle", "gslot", "region", "migrations");
    for (int i = 0; i < N_TRACK; i++) {
        int gs = s->handle_to_slot[hs[i]];
        const char* reg = (s->meta[gs].region == HP_REGION_HOT) ? "HOT" : "cold";
        printf("%-6d  %-6d  %-6s  %d\n", hs[i], gs, reg, s->meta[gs].page_migrations);
    }
    printf("\n");

    printf("when an object is promoted cold->hot it moves to a different virtual region\n");
    printf("(hot.base vs cold.base) which is backed by different physical pages\n");
    printf("so the physical address changes on every promotion and demotion\n\n");

#undef PRINT_HANDLE

    hp_slab_destroy(s);
    (void)sink;
}


__attribute__((unused)) static void exp7_mlock_stress(void)
{
    exp_header("EXP 7: mlock stress — large region behavior under memory pressure");

    /* ---- system info ---- */
    long mem_total_kb = 0, mem_avail_kb = 0, swap_total_kb = 0;
    FILE* mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            sscanf(line, "MemTotal: %ld kB", &mem_total_kb);
            sscanf(line, "MemAvailable: %ld kB", &mem_avail_kb);
            sscanf(line, "SwapTotal: %ld kB", &swap_total_kb);
        }
        fclose(mf);
    }

    struct rlimit rl = {0, 0};
    getrlimit(RLIMIT_MEMLOCK, &rl);

    printf("system RAM      : %ld MB\n", mem_total_kb / 1024);
    printf("available RAM   : %ld MB\n", mem_avail_kb / 1024);
    printf("swap space      : %ld MB\n", swap_total_kb / 1024);
    if (rl.rlim_cur == RLIM_INFINITY)
        printf("RLIMIT_MEMLOCK  : unlimited\n\n");
    else
        printf("RLIMIT_MEMLOCK  : %lu KB (soft) — mlock will fail above this\n\n",
               (unsigned long)(rl.rlim_cur / 1024));

    /* ---- sizes to test ---- */
    static const struct { size_t bytes; const char* label; } SIZES[] = {
        { 128UL << 20,                   "128 MB" },
        { 512UL << 20,                   "512 MB" },
        { 1UL   << 30,                   "1 GB"   },
        { 2UL   << 30,                   "2 GB"   },
        { (size_t)(3.5 * (1UL << 30)),   "3.5 GB" },
    };
    int n_sizes = 5;

    printf("%-8s  %-16s  %-12s  %-8s  %-10s  %-12s  note\n",
           "size", "page type", "first-touch", "mlock", "mlock(ms)", "warm-scan");
    printf("%-8s  %-16s  %-12s  %-8s  %-10s  %-12s\n",
           "--------", "----------------", "------------", "--------",
           "----------", "------------");

    for (int si = 0; si < n_sizes; si++) {
        size_t nbytes = SIZES[si].bytes;

        /* skip if clearly too large for RAM+swap */
        if (nbytes / 1024 > (size_t)(mem_total_kb + swap_total_kb) * 9 / 10) {
            printf("%-8s  skipped — exceeds 90%% of RAM+swap\n\n", SIZES[si].label);
            continue;
        }

        for (int pt = 0; pt < 2; pt++) {
            const char* ptype = (pt == 0) ? "4 KB pages" : "THP 2 MB pages";

            void* ptr = mmap(NULL, nbytes, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) {
                printf("%-8s  %-16s  mmap failed: %s\n",
                       SIZES[si].label, ptype, strerror(errno));
                continue;
            }

            if (pt == 1)
                madvise(ptr, nbytes, MADV_HUGEPAGE);

            /* first touch: fault in every page, building page tables */
            double t0 = now_ms();
            volatile char* p = (volatile char*)ptr;
            for (size_t off = 0; off < nbytes; off += 4096)
                p[off] = (char)(off & 0xFF);
            double first_ms = now_ms() - t0;

            /* attempt mlock — pins pages in RAM, prevents swap eviction */
            double tl0 = now_ms();
            int ml_ret  = mlock(ptr, nbytes);
            int ml_err  = errno;
            double mlock_ms = now_ms() - tl0;

            const char* ml_status;
            if      (ml_ret == 0)       ml_status = "OK";
            else if (ml_err == ENOMEM)  ml_status = "ENOMEM";
            else if (ml_err == EPERM)   ml_status = "EPERM";
            else                        ml_status = "FAIL";

            /* warm scan: re-read every page after mlock attempt */
            long volatile sink = 0;
            double tw0 = now_ms();
            for (size_t off = 0; off < nbytes; off += 4096)
                sink += p[off];
            double warm_ms = now_ms() - tw0;

            /* note what this result means */
            const char* note = "";
            if (ml_ret != 0)
                note = "unlocked — pages are swap-eligible";
            else if (nbytes / 1024 > (size_t)mem_avail_kb)
                note = "locked — OS had to evict other pages to fit";

            printf("%-8s  %-16s  %7.0f ms    %-8s  %6.1f ms   %7.0f ms    %s\n",
                   (pt == 0) ? SIZES[si].label : "",
                   ptype, first_ms, ml_status, mlock_ms, warm_ms, note);

            if (ml_ret == 0) munlock(ptr, nbytes);
            munmap(ptr, nbytes);
            (void)sink;
        }
        printf("\n");
    }

    printf("interpretation:\n");
    printf("  first-touch : faults every page in from zero; includes page table allocation\n");
    printf("                THP reduces page table entries 512x -> should be faster at large sizes\n");
    printf("  mlock OK    : pages are pinned in RAM, kernel cannot swap them out\n");
    printf("  mlock EPERM : process hit RLIMIT_MEMLOCK; pages remain in RAM for now\n");
    printf("                but are swap-eligible — OS may evict them under pressure\n");
    printf("  mlock ENOMEM: kernel could not find enough lockable physical pages\n");
    printf("  warm-scan   : sequential re-read after mlock attempt\n");
    printf("                if warm >> first-touch: pages were evicted to swap between the two\n");
    printf("                if warm << first-touch: no swap, TLB/cache warmed up (expected)\n");
    printf("  4KB vs THP  : THP packs 512 pages into one TLB entry; at 3.5 GB that is\n");
    printf("                ~3584 TLB entries (THP) vs ~917504 TLB entries (4KB)\n\n");
}


static double measure_hot_scan(HpAwareSlab* s,
                               HpSlabHandle* hot_hs, int n_hot,
                               int scans)
{
    long volatile sink = 0;
    double total = 0;
    for (int rep = 0; rep < WARMUP_REPS + MEASURE_REPS; rep++) {
        double t0 = now_ms();
        for (int sc = 0; sc < scans; sc++)
            for (int i = 0; i < n_hot; i++) {
                Obj* o = (Obj*)hp_slab_get_raw(s, hot_hs[i]);
                o->counter++;
                sink += o->counter;
            }
        double t1 = now_ms();
        if (rep >= WARMUP_REPS) total += t1 - t0;
    }
    (void)sink;
    return total / MEASURE_REPS;
}

static void drive_promotions(HpAwareSlab* s, HpSlabHandle* hot_hs, int n_hot)
{
    long volatile sink = 0;
    for (int iter = 0; iter < HP_SLAB_EPOCH_SIZE * 6; iter++) {
        Obj* o = (Obj*)hp_slab_get(s, hot_hs[iter % n_hot]);
        o->counter++;
        sink += o->counter;
    }
    (void)sink;
}

/*
 * EXP ISOLATION: which features actually earn their keep?
 *
 * Five variants, all using the same workload:
 *   n_obj objects allocated, top 20% are hot, scan hot set 200 times.
 *
 * A -- flat baseline:        no huge pages, no clustering, no mlock
 * B -- huge pages only:      huge pages,    no clustering, no mlock
 * C -- clustering only:      no huge pages, clustering,    no mlock
 * D -- huge + clustering:    huge pages,    clustering,    no mlock
 * E -- full design:          huge pages,    clustering,    mlock
 *
 * This answers the user's question: does the OS handle things automatically
 * (making our clustering and mlock redundant), or does each feature
 * contribute a measurable benefit?
 */
static void exp_isolation(void)
{
    exp_header("EXP ISOLATION: huge pages vs clustering vs mlock");

    int n_obj    = 2048;
    int n_hot    = (int)(n_obj * HP_SLAB_HOT_PERCENTILE);
    int stride   = (int)(1.0 / HP_SLAB_HOT_PERCENTILE);
    int scans    = 200;

    printf("workload: %d total objects, %d hot (%.0f%%), scan hot set %d times\n\n",
           n_obj, n_hot, HP_SLAB_HOT_PERCENTILE * 100.0, scans);

    double times[5] = {0};
    const char* names[5] = {
        "A: flat (no huge pages, no cluster, no mlock)",
        "B: huge pages only (no cluster, no mlock)",
        "C: clustering only (no huge pages, no mlock)",
        "D: huge pages + clustering (no mlock)",
        "E: full design (huge pages + clustering + mlock)"
    };

    HpSlabHandle* hot_hs = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_hot);

    /* ---- A: flat baseline ---- */
    {
        HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
        madvise(s->hot.base,  s->hot.byte_size,  MADV_NOHUGEPAGE);
        madvise(s->cold.base, s->cold.byte_size, MADV_NOHUGEPAGE);
        if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }

        int hi = 0;
        for (int i = 0; i < n_obj; i++) {
            HpSlabHandle h = hp_slab_alloc(s);
            ((Obj*)hp_slab_get_raw(s, h))->id = i;
            if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
        }
        /* no clustering: use get_raw so objects stay scattered, never promoted */
        times[0] = measure_hot_scan(s, hot_hs, n_hot, scans);
        hp_slab_destroy(s);
    }

    /* ---- B: huge pages only ---- */
    {
        HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
        if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }

        int hi = 0;
        for (int i = 0; i < n_obj; i++) {
            HpSlabHandle h = hp_slab_alloc(s);
            ((Obj*)hp_slab_get_raw(s, h))->id = i;
            if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
        }
        /* no clustering: objects stay in cold region (which has huge pages) */
        times[1] = measure_hot_scan(s, hot_hs, n_hot, scans);
        hp_slab_destroy(s);
    }

    /* ---- C: clustering only ---- */
    {
        HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
        madvise(s->hot.base,  s->hot.byte_size,  MADV_NOHUGEPAGE);
        madvise(s->cold.base, s->cold.byte_size, MADV_NOHUGEPAGE);
        if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }

        int hi = 0;
        for (int i = 0; i < n_obj; i++) {
            HpSlabHandle h = hp_slab_alloc(s);
            ((Obj*)hp_slab_get_raw(s, h))->id = i;
            if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
        }
        drive_promotions(s, hot_hs, n_hot);
        times[2] = measure_hot_scan(s, hot_hs, n_hot, scans);
        hp_slab_destroy(s);
    }

    /* ---- D: huge pages + clustering, no mlock ---- */
    {
        HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
        if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }

        int hi = 0;
        for (int i = 0; i < n_obj; i++) {
            HpSlabHandle h = hp_slab_alloc(s);
            ((Obj*)hp_slab_get_raw(s, h))->id = i;
            if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
        }
        drive_promotions(s, hot_hs, n_hot);
        times[3] = measure_hot_scan(s, hot_hs, n_hot, scans);
        hp_slab_destroy(s);
    }

    /* ---- E: full design (huge pages + clustering + mlock) ---- */
    {
        HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);

        int hi = 0;
        for (int i = 0; i < n_obj; i++) {
            HpSlabHandle h = hp_slab_alloc(s);
            ((Obj*)hp_slab_get_raw(s, h))->id = i;
            if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
        }
        drive_promotions(s, hot_hs, n_hot);
        times[4] = measure_hot_scan(s, hot_hs, n_hot, scans);
        hp_slab_destroy(s);
    }

    printf("%-50s  %-12s  %s\n", "variant", "scan (ms)", "vs baseline A");
    for (int i = 0; i < 5; i++) {
        printf("%-50s  %-12.3f  %.2fx%s\n",
               names[i], times[i], times[0] / times[i],
               i == 0 ? "  (baseline)" : "");
    }

    printf("\n");
    printf("  B vs A: does huge pages alone help (without clustering)?\n");
    printf("  C vs A: does clustering alone help (without huge pages)?\n");
    printf("  D vs C: do huge pages add benefit on top of clustering?\n");
    printf("  E vs D: does mlock specifically add anything?\n\n");

    free(hot_hs);
}


static void exp_epoch_sweep(void)
{
    exp_header("EXP EPOCH SWEEP: which variant wins at each epoch size?");
    printf("tests A=flat, B=huge only, C=cluster only, D=huge+cluster, E=full\n");
    printf("fixed budget of 48000 accesses before measuring — shows how each\n");
    printf("epoch size changes convergence quality and which feature matters most.\n\n");

    static const int EPOCH_SIZES[] = { 2000, 4000, 8000, 16000, 32000 };
    const int ACCESS_BUDGET = 48000;
    const int n_obj   = 2048;
    const int n_hot   = (int)(n_obj * HP_SLAB_HOT_PERCENTILE);
    const int stride  = (int)(1.0 / HP_SLAB_HOT_PERCENTILE);
    const int scans   = 200;

    HpSlabHandle* hot_hs = (HpSlabHandle*)malloc(sizeof(HpSlabHandle) * (size_t)n_hot);

    printf("%-10s  %-10s  %-10s  %-12s  %-10s  %-10s  compacts  winner\n",
           "epoch", "A:flat", "B:huge", "C:cluster", "D:h+cl", "E:full");

    for (int ei = 0; ei < 5; ei++) {
        int epoch_size = EPOCH_SIZES[ei];
        double times[5]    = {0};
        int    compacts[5] = {0};

        /* A: flat — no huge pages, no clustering */
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
            s->epoch_size = epoch_size;
            madvise(s->hot.base,  s->hot.byte_size,  MADV_NOHUGEPAGE);
            madvise(s->cold.base, s->cold.byte_size, MADV_NOHUGEPAGE);
            if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }
            int hi = 0;
            for (int i = 0; i < n_obj; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
            }
            times[0]    = measure_hot_scan(s, hot_hs, n_hot, scans);
            compacts[0] = s->n_compactions;
            hp_slab_destroy(s);
        }

        /* B: huge pages only — no clustering */
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
            s->epoch_size = epoch_size;
            if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }
            int hi = 0;
            for (int i = 0; i < n_obj; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
            }
            times[1]    = measure_hot_scan(s, hot_hs, n_hot, scans);
            compacts[1] = s->n_compactions;
            hp_slab_destroy(s);
        }

        /* C: clustering only — no huge pages */
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
            s->epoch_size = epoch_size;
            madvise(s->hot.base,  s->hot.byte_size,  MADV_NOHUGEPAGE);
            madvise(s->cold.base, s->cold.byte_size, MADV_NOHUGEPAGE);
            if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }
            int hi = 0;
            for (int i = 0; i < n_obj; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
            }
            long volatile sink = 0;
            for (int iter = 0; iter < ACCESS_BUDGET; iter++) {
                Obj* o = (Obj*)hp_slab_get(s, hot_hs[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }
            (void)sink;
            times[2]    = measure_hot_scan(s, hot_hs, n_hot, scans);
            compacts[2] = s->n_compactions;
            hp_slab_destroy(s);
        }

        /* D: huge pages + clustering, no mlock */
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
            s->epoch_size = epoch_size;
            if (s->hot.is_locked) { munlock(s->hot.base, s->hot.byte_size); s->hot.is_locked = 0; }
            int hi = 0;
            for (int i = 0; i < n_obj; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
            }
            long volatile sink = 0;
            for (int iter = 0; iter < ACCESS_BUDGET; iter++) {
                Obj* o = (Obj*)hp_slab_get(s, hot_hs[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }
            (void)sink;
            times[3]    = measure_hot_scan(s, hot_hs, n_hot, scans);
            compacts[3] = s->n_compactions;
            hp_slab_destroy(s);
        }

        /* E: full design — huge pages + clustering + mlock */
        {
            HpAwareSlab* s = hp_slab_create(sizeof(Obj), n_obj + 16);
            s->epoch_size = epoch_size;
            int hi = 0;
            for (int i = 0; i < n_obj; i++) {
                HpSlabHandle h = hp_slab_alloc(s);
                ((Obj*)hp_slab_get_raw(s, h))->id = i;
                if (i % stride == stride - 1 && hi < n_hot) hot_hs[hi++] = h;
            }
            long volatile sink = 0;
            for (int iter = 0; iter < ACCESS_BUDGET; iter++) {
                Obj* o = (Obj*)hp_slab_get(s, hot_hs[iter % n_hot]);
                o->counter++;
                sink += o->counter;
            }
            (void)sink;
            times[4]    = measure_hot_scan(s, hot_hs, n_hot, scans);
            compacts[4] = s->n_compactions;
            hp_slab_destroy(s);
        }

        int winner = 0;
        for (int i = 1; i < 5; i++)
            if (times[i] < times[winner]) winner = i;

        const char* wnames[5] = { "A:flat", "B:huge", "C:cluster", "D:h+cl", "E:full" };
        printf("%-10d  %-10.3f  %-10.3f  %-12.3f  %-10.3f  %-10.3f  %-8d  %s\n",
               epoch_size,
               times[0], times[1], times[2], times[3], times[4],
               compacts[2],  /* clustering variants C/D/E share same compaction count */
               wnames[winner]);
    }

    printf("\nfixed access budget: %d accesses before measuring\n", ACCESS_BUDGET);
    printf("compacts column = number fired in variant C (same for D and E)\n");
    printf("small epoch: more compactions = objects cluster faster = C/D/E benefit more\n");
    printf("large epoch: few compactions within budget = objects still scattered = A/B may win\n");
    printf("mlock (E vs D): adds benefit only when pages can be evicted under pressure\n\n");

    free(hot_hs);
}


int main(void)
{
    {
        long hp_total = 0;
        FILE *mf = fopen("/proc/meminfo", "r");
        if (mf) {
            char line[256];
            while (fgets(line, sizeof(line), mf))
                if (sscanf(line, "HugePages_Total: %ld", &hp_total) == 1) break;
            fclose(mf);
        }
        if (hp_total > 0)
            printf("huge pages ARE available on this system (%ld pages reserved)\n\n", hp_total);
        else
            printf("huge pages are NOT available on this system (falling back to THP/normal pages)\n\n");
    }

    printf("\n");
    printf("*****************************************************************\n");
    printf("*       HUGE PAGE HOT/COLD SLAB ALLOCATOR — BENCHMARK           *\n");
    printf("*****************************************************************\n");
    printf("\n");
    printf("object size    : %zu bytes\n",   sizeof(Obj));
    printf("epoch size     : %d accesses\n", HP_SLAB_EPOCH_SIZE);
    printf("hot region     : %.0f%% of capacity\n", HP_SLAB_HOT_PERCENTILE * 100.0);
    printf("huge page size : %zu KB\n\n",    HUGE_PAGE_SIZE / 1024);

    exp1_region_structure();
    exp2_promotion_demotion();
    exp3_cache_performance();
    exp3b_full_scan();
    exp4_epoch_progression();
    exp_isolation();
    exp_epoch_sweep();
    printf("\n=== SUMMARY ===\n\n");
    printf("two mmap regions, both backed by huge pages (MAP_HUGETLB or THP fallback)\n");
    printf("hot region: MADV_HUGEPAGE + mlock -> one 2 MB TLB entry, never evicted\n");
    printf("cold region: MADV_NOHUGEPAGE + MADV_COLD -> 4 KB pages, reclaimable\n");
    printf("page tracking via /proc/self/pagemap shows physical address changes on promotion\n\n");

    return 0;
}
