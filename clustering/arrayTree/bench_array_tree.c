#include "array_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define _POSIX_C_SOURCE 200809L
#define WARMUP_REPS   3
#define MEASURE_REPS  10

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static void separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}

static void header(const char* t) {
    separator();
    printf("  %s\n", t);
    separator();
}

#define BENCH_AVG(result_avg, code)                              \
    do {                                                         \
        long volatile _sink = 0;                                 \
        double _total = 0;                                       \
        for (int _r = 0; _r < WARMUP_REPS + MEASURE_REPS; _r++) { \
            double _t0 = now_ms();                               \
            _sink += (code);                                     \
            double _t1 = now_ms();                               \
            if (_r >= WARMUP_REPS) _total += (_t1 - _t0);       \
        }                                                        \
        (result_avg) = _total / MEASURE_REPS;                    \
        (void)_sink;                                             \
    } while(0)

static const int SIZES[] = { 10000, 50000, 100000, 500000 };
#define N_SIZES (int)(sizeof(SIZES)/sizeof(SIZES[0]))

static int tree_capacity(int size) {
    int cap = 1;
    while (cap < size * 3)   
        cap = cap * 2 + 1;   
    return cap;
}

static void shuffle(int* arr, int n) {
    for (int i = n-1; i > 0; i--) {
        int j   = rand() % (i+1);
        int tmp = arr[i];
        arr[i]  = arr[j];
        arr[j]  = tmp;
    }
}

static void exp1_layout(void) {
    header("EXPERIMENT 1 — Memory layout");
    printf("  How each tree stores its nodes in memory.\n\n");

    int cap = tree_capacity(10000);
    ArrayTree   at; atree_init (&at, cap);
    PointerTree pt; ptree_init (&pt, 10000);

    print_tree_layout_info(&at, &pt);

    atree_destroy(&at);
    ptree_destroy(&pt);
}

static void exp2_traversal(void) {
    header("EXPERIMENT 2 — Traversal performance (visit every node)");
    printf("  ArrayTree traversal = linear array scan (sequential access).\n"
           "  PointerTree traversal = follow left/right pointers.\n"
           "  Average of %d runs shown.\n\n", MEASURE_REPS);

    printf("  %-10s  %-22s  %-22s  %10s\n",
           "Size", "ArrayTree (ms)", "PointerTree (ms)", "Speedup");
    printf("  %-10s  %-22s  %-22s  %10s\n",
           "────", "──────────────", "────────────────", "───────");

    double speedups[N_SIZES];

    for (int si = 0; si < N_SIZES; si++) {
        int n   = SIZES[si];
        int cap = tree_capacity(n);

        int* vals = (int*)malloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; i++) vals[i] = i + 1;
        shuffle(vals, n);

        ArrayTree   at; atree_init (&at, cap);
        PointerTree pt; ptree_init (&pt, n);

        for (int i = 0; i < n; i++) {
            atree_insert (&at, vals[i]);
            ptree_insert (&pt, vals[i]);
        }

        double t_array, t_pointer;
        BENCH_AVG(t_array,   atree_traverse_sum(&at));
        BENCH_AVG(t_pointer, ptree_traverse_sum(&pt));

        speedups[si] = t_pointer / t_array;

        printf("  %-10d  %-22.3f  %-22.3f  %9.2fx\n",
               n, t_array, t_pointer, speedups[si]);

        free(vals);
        atree_destroy(&at);
        ptree_destroy(&pt);
    }

    double avg = 0;
    for (int i = 0; i < N_SIZES; i++) avg += speedups[i];
    avg /= N_SIZES;
    printf("\n  Average traversal speedup (ArrayTree vs PointerTree): %.2fx\n\n",
           avg);
}

static void exp3_search(void) {
    header("EXPERIMENT 3 — Search performance");
    printf("  Search for every 7th value.\n"
           "  ArrayTree navigates with index arithmetic (2*i+1, 2*i+2).\n"
           "  PointerTree follows left/right pointers.\n"
           "  Average of %d runs shown.\n\n", MEASURE_REPS);

    printf("  %-10s  %-22s  %-22s  %10s\n",
           "Size", "ArrayTree (ms)", "PointerTree (ms)", "Speedup");
    printf("  %-10s  %-22s  %-22s  %10s\n",
           "────", "──────────────", "────────────────", "───────");

    double speedups[N_SIZES];

    for (int si = 0; si < N_SIZES; si++) {
        int n        = SIZES[si];
        int cap      = tree_capacity(n);
        int n_search = n / 7 + 1;

        int* vals = (int*)malloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; i++) vals[i] = i + 1;
        shuffle(vals, n);

        ArrayTree   at; atree_init (&at, cap);
        PointerTree pt; ptree_init (&pt, n);

        for (int i = 0; i < n; i++) {
            atree_insert (&at, vals[i]);
            ptree_insert (&pt, vals[i]);
        }

        double t_array, t_pointer;
        BENCH_AVG(t_array, ({
            long s = 0;
            for (int i = 0; i < n_search; i++)
                s += atree_search(&at, (i * 7 % n) + 1);
            s;
        }));
        BENCH_AVG(t_pointer, ({
            long s = 0;
            for (int i = 0; i < n_search; i++)
                s += ptree_search(&pt, (i * 7 % n) + 1);
            s;
        }));

        speedups[si] = t_pointer / t_array;

        printf("  %-10d  %-22.3f  %-22.3f  %9.2fx\n",
               n, t_array, t_pointer, speedups[si]);

        free(vals);
        atree_destroy(&at);
        ptree_destroy(&pt);
    }

    double avg = 0;
    for (int i = 0; i < N_SIZES; i++) avg += speedups[i];
    avg /= N_SIZES;
    printf("\n  Average search speedup (ArrayTree vs PointerTree): %.2fx\n\n",
           avg);
}

static void exp4_insert(void) {
    header("EXPERIMENT 4 — Insert performance");
    printf("  Insert N values one by one.\n"
           "  ArrayTree: walk array with index arithmetic.\n"
           "  PointerTree: walk pointers, allocate from pool.\n"
           "  Average of %d runs shown.\n\n", MEASURE_REPS);

    printf("  %-10s  %-22s  %-22s  %10s\n",
           "Size", "ArrayTree (ms)", "PointerTree (ms)", "Speedup");
    printf("  %-10s  %-22s  %-22s  %10s\n",
           "────", "──────────────", "────────────────", "───────");

    double speedups[N_SIZES];

    for (int si = 0; si < N_SIZES; si++) {
        int n   = SIZES[si];
        int cap = tree_capacity(n);

        int* vals = (int*)malloc(sizeof(int) * (size_t)n);
        for (int i = 0; i < n; i++) vals[i] = i + 1;
        shuffle(vals, n);

        ArrayTree   at; atree_init (&at, cap);
        PointerTree pt; ptree_init (&pt, n);

        double t_array, t_pointer;

        BENCH_AVG(t_array, ({
            atree_clear(&at);
            for (int i = 0; i < n; i++) atree_insert(&at, vals[i]);
            0L;
        }));

        BENCH_AVG(t_pointer, ({
            ptree_clear(&pt);
            for (int i = 0; i < n; i++) ptree_insert(&pt, vals[i]);
            0L;
        }));

        speedups[si] = t_pointer / t_array;

        printf("  %-10d  %-22.3f  %-22.3f  %9.2fx\n",
               n, t_array, t_pointer, speedups[si]);

        free(vals);
        atree_destroy(&at);
        ptree_destroy(&pt);
    }

    double avg = 0;
    for (int i = 0; i < N_SIZES; i++) avg += speedups[i];
    avg /= N_SIZES;
    printf("\n  Average insert speedup (ArrayTree vs PointerTree): %.2fx\n\n",
           avg);
}

static void exp5_summary(void) {
    header("EXPERIMENT 5 — Cache line utilization");
    printf("  How many values fit in one 64-byte cache line?\n\n");

    printf("  %-20s  %12s  %12s  %16s\n",
           "Tree type", "Node size", "Per cache line", "What loads with it");
    printf("  %-20s  %12s  %12s  %16s\n",
           "─────────", "─────────", "──────────────", "──────────────────");
    printf("  %-20s  %12zu  %12zu  %s\n",
           "ArrayTree",
           sizeof(int32_t),
           64 / sizeof(int32_t),
           "15 neighboring values");
    printf("  %-20s  %12zu  %12zu  %s\n",
           "PointerTree",
           sizeof(PNode),
           64 / sizeof(PNode),
           "2 neighboring nodes");
    printf("\n");
    printf("  ArrayTree stores 16 values per cache line vs 2 nodes.\n");
    printf("  When you access one value, 15 neighbors arrive for free.\n");
    printf("  When you follow a pointer, you get 1 useful node + wasted space.\n\n");
}


int main(void) {
    srand(42);  

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║        ARRAY TREE BENCHMARK — Cache-Friendly Allocator            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  sizeof(int32_t) = %zu bytes  (ArrayTree node — value only)\n",
           sizeof(int32_t));
    printf("  sizeof(PNode)   = %zu bytes  (PointerTree node — value + 2 ptrs)\n",
           sizeof(PNode));
    printf("  Cache line      = 64 bytes\n");
    printf("  ArrayTree  fits %zu values  per cache line\n",
           64 / sizeof(int32_t));
    printf("  PointerTree fits %zu nodes   per cache line\n\n",
           64 / sizeof(PNode));

    exp1_layout();
    exp2_traversal();
    exp3_search();
    exp4_insert();
    exp5_summary();

    separator();
    printf("\n  KEY INSIGHT\n\n");
    printf("  ArrayTree node = 4 bytes (value only)\n");
    printf("  PointerTree node = 24 bytes (value + left ptr + right ptr)\n");
    printf("  Ratio: 6x more values per cache line in ArrayTree.\n\n");
    printf("  Traversal: ArrayTree scans array sequentially — CPU prefetcher\n");
    printf("  handles it perfectly. PointerTree follows unpredictable pointers.\n\n");
    printf("  Search: both follow the same logical path (O(log n) levels).\n");
    printf("  ArrayTree uses arithmetic instead of pointer dereference at each\n");
    printf("  level — the next index is calculated while current node loads.\n\n");

    return 0;
}

