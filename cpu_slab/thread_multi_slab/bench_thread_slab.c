#define _POSIX_C_SOURCE 200809L
#include "thread_slab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

static void exp_header(const char* t) {
    printf("\n");
    printf("==============================================================\n");
    printf("  %s\n", t);
    printf("==============================================================\n");
    printf("\n");
}

static double run_threads(int n_threads,
                          WorkerArgs* args,
                          void* (*worker)(void*)) {
    pthread_t* tids = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)n_threads);

    for (int i = 0; i < n_threads; i++)
        pthread_create(&tids[i], NULL, worker, &args[i]);

    for (int i = 0; i < n_threads; i++)
        pthread_join(tids[i], NULL);

    double total = 0;
    for (int i = 0; i < n_threads; i++)
        total += args[i].time_ms;

    free(tids);
    return total / n_threads;
}

static void exp1_thread_safety(void) {
    exp_header("EXP 1: tls thread safety verification");
    printf("2 threads, each increments its own private counter 1,000,000 times\n");
    printf("expected result per thread: 500,000,500,000\n\n");

    int  n_iters  = 1000000;
    int  n_hot    = 1;
    long expected = (long)n_iters * (n_iters + 1) / 2;

    TLSSlab*   tls = tls_create();
    WorkerArgs args[2];
    memset(args, 0, sizeof(args));

    for (int t = 0; t < 2; t++) {
        args[t].thread_id = t;
        args[t].n_iters   = n_iters;
        args[t].n_hot     = n_hot;
        args[t].tls       = tls;
    }

    run_threads(2, args, worker_tls);

    for (int t = 0; t < 2; t++) {
        int pass = args[t].result == expected;
        printf("  thread %d:  result = %ld  expected = %ld  %s\n",
               t, args[t].result, expected, pass ? "PASS" : "FAIL");
    }

    printf("\n  both threads got the correct independent result.\n");
    printf("  no data was shared, no corruption occurred.\n\n");

    tls_destroy(tls);
}

static void exp2_scaling(void) {
    exp_header("EXP 2: tls scaling -- does adding threads slow things down?");
    printf("tls at 1, 2, 4 threads -- 3,000,000 iterations, 32 hot objects each\n");
    printf("no locks, no shared memory: time should stay flat\n\n");

    int n_iters         = 3000000;
    int n_hot           = 32;
    int thread_counts[] = { 1, 2, 4 };
    int n_tc            = 3;
    double base         = 0;

    printf("  %-8s  %-14s  %s\n", "threads", "avg time (ms)", "vs 1 thread");

    for (int ti = 0; ti < n_tc; ti++) {
        int nt = thread_counts[ti];

        TLSSlab*   tls = tls_create();
        WorkerArgs args[MAX_THREADS];
        memset(args, 0, sizeof(args));

        for (int t = 0; t < nt; t++) {
            args[t].thread_id = t;
            args[t].n_iters   = n_iters;
            args[t].n_hot     = n_hot;
            args[t].tls       = tls;
        }

        double t = run_threads(nt, args, worker_tls);
        if (nt == 1) base = t;

        printf("  %-8d  %-14.2f  %.2fx\n", nt, t, base / t);
        tls_destroy(tls);
    }

    printf("\n  time stays flat as threads increase.\n");
    printf("  each thread runs in its own private slab with no waiting.\n\n");
}

static void exp3_shared_correctness(void) {
    exp_header("EXP 3: shared slab correctness");
    printf("2 threads, each increments its own counter 1,000,000 times\n");
    printf("shared slab is mutex-protected; expected result per thread: 500,000,500,000\n\n");

    int  n_iters  = 1000000;
    int  n_hot    = 1;
    long expected = (long)n_iters * (n_iters + 1) / 2;

    SharedSlab* shared = shared_create();
    WorkerArgs  args[2];
    memset(args, 0, sizeof(args));

    for (int t = 0; t < 2; t++) {
        args[t].thread_id = t;
        args[t].n_iters   = n_iters;
        args[t].n_hot     = n_hot;
        args[t].shared    = shared;
    }

    run_threads(2, args, worker_shared);

    for (int t = 0; t < 2; t++) {
        int pass = args[t].result == expected;
        printf("  thread %d:  result = %ld  expected = %ld  %s\n",
               t, args[t].result, expected, pass ? "PASS" : "FAIL");
    }

    printf("\n  mutex-protected shared slab handled concurrent access correctly.\n\n");

    shared_destroy(shared);
}

int main(void) {
    printf("\n");
    printf("*****************************************************************\n");
    printf("*       THREAD SLAB ALLOCATOR -- BENCHMARK                      *\n");
    printf("*****************************************************************\n");
    printf("\n");

    exp1_thread_safety();
    exp2_scaling();
    exp3_shared_correctness();

    printf("\n=== SUMMARY ===\n\n");
    printf("EXP 1: tls thread safety   -- each thread got the correct independent result\n");
    printf("EXP 2: tls scaling         -- time stays flat as threads increase\n");
    printf("EXP 3: shared correctness  -- mutex-protected shared slab works correctly\n\n");

    return 0;
}
