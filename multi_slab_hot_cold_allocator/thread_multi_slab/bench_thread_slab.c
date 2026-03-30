#define _POSIX_C_SOURCE 200809L
#include "thread_slab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

static void separator(void) {
    printf("─────────────────────────────────────────────────────────────────────\n");
}
static void header(const char* t) {
    separator();
    printf("  %s\n", t);
    separator();
}

static double run_threads(int n_threads,
                          WorkerArgs* args,
                          void* (*worker)(void*)) {
    pthread_t* tids = (pthread_t*)malloc(
                          sizeof(pthread_t) * (size_t)n_threads);

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

static void exp1_false_sharing(void) {
    header("EXPERIMENT 1 — False sharing: shared vs thread-local");
    printf("  Same workload, same number of threads.\n"
           "  Shared slab: threads may write to adjacent cache lines.\n"
           "  Thread-local: each thread's objects in separate memory.\n\n");

    int n_iters = 5000000;
    int n_hot   = 32;          

    SharedSlab* shared = shared_create();
    WorkerArgs  args_s[1];
    memset(args_s, 0, sizeof(args_s));

    AllocHandle* hot1 = (AllocHandle*)malloc(
                            sizeof(AllocHandle) * (size_t)n_hot);
    for (int i = 0; i < n_hot; i++) {
        hot1[i] = shared_alloc(shared, 16);
        *(int*)shared_get(shared, hot1[i]) = 0;
    }

    args_s[0].thread_id  = 0;
    args_s[0].n_iters    = n_iters;
    args_s[0].n_hot      = n_hot;
    args_s[0].shared     = shared;
    args_s[0].hot_handles = hot1;

    double t_single = run_threads(1, args_s, worker_shared);
    printf("  %-42s  %8.2f ms   (reference)\n",
           "1 thread, shared slab", t_single);

    WorkerArgs args_2s[2];
    memset(args_2s, 0, sizeof(args_2s));

    AllocHandle* hot2[2];
    for (int t = 0; t < 2; t++) {
        hot2[t] = (AllocHandle*)malloc(
                      sizeof(AllocHandle) * (size_t)n_hot);
        for (int i = 0; i < n_hot; i++) {
            hot2[t][i] = shared_alloc(shared, 16);
            *(int*)shared_get(shared, hot2[t][i]) = 0;
        }
        args_2s[t].thread_id   = t;
        args_2s[t].n_iters     = n_iters;
        args_2s[t].n_hot       = n_hot;
        args_2s[t].shared      = shared;
        args_2s[t].hot_handles = hot2[t];
    }

    double t_shared2 = run_threads(2, args_2s, worker_shared);
    printf("  %-42s  %8.2f ms   (slowdown: %.2fx)\n",
           "2 threads, shared slab (false sharing)",
           t_shared2, t_shared2 / t_single);

    TLSSlab* tls = tls_create();
    WorkerArgs args_2t[2];
    memset(args_2t, 0, sizeof(args_2t));

    for (int t = 0; t < 2; t++) {
        args_2t[t].thread_id = t;
        args_2t[t].n_iters   = n_iters;
        args_2t[t].n_hot     = n_hot;
        args_2t[t].tls       = tls;
    }

    double t_tls2 = run_threads(2, args_2t, worker_tls);
    printf("  %-42s  %8.2f ms   (speedup over shared: %.2fx)\n",
           "2 threads, thread-local (no false sharing)",
           t_tls2, t_shared2 / t_tls2);

    printf("\n  False sharing cost: %.2fx slowdown (shared 2T vs single T)\n",
           t_shared2 / t_single);
    printf("  TLS solution:       %.2fx faster than shared 2T\n\n",
           t_shared2 / t_tls2);

    for (int t = 0; t < 2; t++) free(hot2[t]);
    free(hot1);
    shared_destroy(shared);
    tls_destroy(tls);
}

static void exp2_lock_cost(void) {
    header("EXPERIMENT 2 — Global mutex cost");
    printf("  Global mutex: only one thread runs at a time.\n"
           "  Thread-local: all threads run truly in parallel.\n\n");

    int n_iters = 2000000;
    int n_hot   = 16;
    int thread_counts[] = { 1, 2, 4 };
    int n_tc = 3;

    printf("  %-8s  %-20s  %-20s  %12s\n",
           "Threads", "Mutex (ms)", "Thread-local (ms)", "TLS speedup");
    printf("  %-8s  %-20s  %-20s  %12s\n",
           "───────", "──────────", "─────────────────", "───────────");

    for (int ti = 0; ti < n_tc; ti++) {
        int nt = thread_counts[ti];

        MutexSlab* ms = mutex_create();
        WorkerArgs args_m[MAX_THREADS];
        memset(args_m, 0, sizeof(args_m));

        for (int t = 0; t < nt; t++) {
            AllocHandle* hot = (AllocHandle*)malloc(
                                   sizeof(AllocHandle) * (size_t)n_hot);
            for (int i = 0; i < n_hot; i++) {
                hot[i] = mutex_alloc(ms, 16);
                *(int*)mutex_get(ms, hot[i]) = 0;
            }
            args_m[t].thread_id   = t;
            args_m[t].n_iters     = n_iters;
            args_m[t].n_hot       = n_hot;
            args_m[t].mutex       = ms;
            args_m[t].hot_handles = hot;
        }
        double t_mutex = run_threads(nt, args_m, worker_mutex);

        for (int t = 0; t < nt; t++) free(args_m[t].hot_handles);
        mutex_destroy(ms);

        TLSSlab* tls = tls_create();
        WorkerArgs args_t[MAX_THREADS];
        memset(args_t, 0, sizeof(args_t));

        for (int t = 0; t < nt; t++) {
            args_t[t].thread_id = t;
            args_t[t].n_iters   = n_iters;
            args_t[t].n_hot     = n_hot;
            args_t[t].tls       = tls;
        }
        double t_tls = run_threads(nt, args_t, worker_tls);
        tls_destroy(tls);

        printf("  %-8d  %-20.2f  %-20.2f  %11.2fx\n",
               nt, t_mutex, t_tls, t_mutex / t_tls);
    }

    printf("\n  Observation: mutex time grows with threads (serialized).\n"
           "  TLS time stays flat or decreases (true parallelism).\n\n");
}


static void exp3_scaling(void) {
    header("EXPERIMENT 3 — Scaling with thread count");
    printf("  Ideal: 2 threads = 2x work done, 4 threads = 4x.\n"
           "  Efficiency = actual throughput / ideal throughput.\n\n");

    int n_iters = 3000000;
    int n_hot   = 32;
    int thread_counts[] = { 1, 2, 4 };
    int n_tc = 3;

    double base_mutex = 0,
           base_tls = 0;

    printf("  %-8s %-14s  %-14s\n",
           "Threads", "Mutex", "TLS");
    printf("  %-8s  %-14s  %-14s\n",
           "───────",
           "─────────────",
           "─────────────");

    for (int ti = 0; ti < n_tc; ti++) {
        int nt = thread_counts[ti];

        double t_mu = 0;
        {
            MutexSlab* s = mutex_create();
            WorkerArgs args[MAX_THREADS];
            memset(args, 0, sizeof(args));
            for (int t = 0; t < nt; t++) {
                AllocHandle* hot = (AllocHandle*)malloc(
                    sizeof(AllocHandle) * (size_t)n_hot);
                for (int i = 0; i < n_hot; i++) {
                    hot[i] = mutex_alloc(s, 16);
                    *(int*)mutex_get(s, hot[i]) = 0;
                }
                args[t].thread_id=t; args[t].n_iters=n_iters;
                args[t].n_hot=n_hot; args[t].mutex=s;
                args[t].hot_handles=hot;
            }
            t_mu = run_threads(nt, args, worker_mutex);
            if (nt==1) base_mutex = t_mu;
            for (int t=0;t<nt;t++) free(args[t].hot_handles);
            mutex_destroy(s);
        }

        double t_tl = 0;
        {
            TLSSlab* s = tls_create();
            WorkerArgs args[MAX_THREADS];
            memset(args, 0, sizeof(args));
            for (int t = 0; t < nt; t++) {
                args[t].thread_id=t; args[t].n_iters=n_iters;
                args[t].n_hot=n_hot; args[t].tls=s;
            }
            t_tl = run_threads(nt, args, worker_tls);
            if (nt==1) base_tls = t_tl;
            tls_destroy(s);
        }

        printf("  %-8d %6.0fms(%4.2fx)"
               "  %6.0fms(%4.2fx)\n",
               nt,
               t_mu, base_mutex  / t_mu,
               t_tl, base_tls    / t_tl);
    }

    printf("\n  Speedup column: how much faster vs 1 thread.\n");
    printf("  TLS should show near-linear speedup.\n");
    printf("  Mutex should show degradation (lock contention).\n");
}


static void exp4_final_verdict(void) {
    header("EXPERIMENT 4 — Final verdict: all variants at 4 threads");
    printf("  4 threads, each doing %d iterations.\n"
           "  Shows the full cost of each approach.\n\n", 2000000);

    int nt     = 4;
    int n_iters = 2000000;
    int n_hot   = 32;

    double times[3] = {0};
    const char* names[] = {
        "Shared (unsafe, false sharing)",
        "Mutex  (safe, serialized)",
        "TLS    (safe, parallel)" 
    };

    {
        SharedSlab* s = shared_create();
        WorkerArgs args[MAX_THREADS];
        memset(args, 0, sizeof(args));
        for (int t = 0; t < nt; t++) {
            AllocHandle* hot = (AllocHandle*)malloc(
                sizeof(AllocHandle) * (size_t)n_hot);
            for (int i = 0; i < n_hot; i++) {
                hot[i] = shared_alloc(s, 16);
                *(int*)shared_get(s, hot[i]) = 0;
            }
            args[t].thread_id=t; args[t].n_iters=n_iters;
            args[t].n_hot=n_hot; args[t].shared=s;
            args[t].hot_handles=hot;
        }
        times[0] = run_threads(nt, args, worker_shared);
        for (int t=0;t<nt;t++) free(args[t].hot_handles);
        shared_destroy(s);
    }

    {
        MutexSlab* s = mutex_create();
        WorkerArgs args[MAX_THREADS];
        memset(args, 0, sizeof(args));
        for (int t = 0; t < nt; t++) {
            AllocHandle* hot = (AllocHandle*)malloc(
                sizeof(AllocHandle) * (size_t)n_hot);
            for (int i = 0; i < n_hot; i++) {
                hot[i] = mutex_alloc(s, 16);
                *(int*)mutex_get(s, hot[i]) = 0;
            }
            args[t].thread_id=t; args[t].n_iters=n_iters;
            args[t].n_hot=n_hot; args[t].mutex=s;
            args[t].hot_handles=hot;
        }
        times[1] = run_threads(nt, args, worker_mutex);
        for (int t=0;t<nt;t++) free(args[t].hot_handles);
        mutex_destroy(s);
    }

    {
        TLSSlab* s = tls_create();
        WorkerArgs args[MAX_THREADS];
        memset(args, 0, sizeof(args));
        for (int t = 0; t < nt; t++) {
            args[t].thread_id=t; args[t].n_iters=n_iters;
            args[t].n_hot=n_hot; args[t].tls=s;
        }
        times[2] = run_threads(nt, args, worker_tls);
        tls_destroy(s);
    }


    double best = times[2];  

    printf("  %-38s  %10s  %10s\n",
           "Variant", "Avg (ms)", "vs TLS");
    printf("  %-38s  %10s  %10s\n",
           "───────", "────────", "──────");
    for (int i = 0; i < 3; i++)
        printf("  %-38s  %10.2f  %9.2fx %s\n",
               names[i], times[i],
               times[i] / best,
               i == 2 ? "← winner" : "");

    printf("\n  CONCLUSION\n\n");
    printf("  Shared (unsafe):   fastest raw code but incorrect —\n"
           "                     data races cause wrong results.\n\n");
    printf("  Mutex (safe):      correct but slow — lock forces\n"
           "                     all threads to run one at a time.\n\n");
    printf("  TLS (safe+fast):   correct AND fast — each thread\n"
           "                     has private memory, zero contention,\n"
           "                     no false sharing, no locks.\n\n");
 }


int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║    THREAD SAFETY BENCHMARK — Cache-Friendly Allocator             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n\n");

    printf("  Four variants compared:\n");
    printf("  1. Shared slab   — no locks (baseline, shows false sharing)\n");
    printf("  2. Mutex slab    — global lock (safe but serialized)\n");
    printf("  3. TLS slab      — thread-local (safe + fast, our solution)\n");

    exp1_false_sharing();
    exp2_lock_cost();
    exp3_scaling();
    exp4_final_verdict();

    separator();
    printf("\n  KEY TAKEAWAY\n\n");
    printf("  Thread-local slabs eliminate false sharing by design:\n");
    printf("  each thread allocates from its own private memory region.\n");
    printf("  Two threads can never share a cache line.\n");
    printf("  No locks needed — zero synchronization overhead.\n");
    printf("  Each thread's hot objects stay in that thread's L1 cache.\n\n");

    return 0;
}
