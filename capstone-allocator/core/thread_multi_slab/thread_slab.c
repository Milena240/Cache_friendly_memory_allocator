#ifndef __THREAD_SLAB_C__
#define __THREAD_SLAB_C__

#define _POSIX_C_SOURCE 200809L
#include "thread_slab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

SharedSlab*
shared_create(void) 
{
    SharedSlab* s = (SharedSlab*)calloc(1, sizeof(SharedSlab));
    s->slab = mslab_create();
    return s;
}

void 
shared_destroy(SharedSlab* s)
{
    mslab_destroy(s->slab);
    free(s);
}

AllocHandle 
shared_alloc(SharedSlab* s, size_t size)
{
    return mslab_alloc(s->slab, size);
}

void* 
shared_get(SharedSlab* s, AllocHandle h)
{
    return mslab_get(s->slab, h);
}

MutexSlab*
mutex_create(void)
{
    MutexSlab* s = (MutexSlab*)calloc(1, sizeof(MutexSlab));
    s->slab = mslab_create();
    pthread_mutex_init(&s->lock, NULL);
    return s;
}

void 
mutex_destroy(MutexSlab* s)
{
    mslab_destroy(s->slab);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

AllocHandle
mutex_alloc(MutexSlab* s, size_t size)
{
    pthread_mutex_lock(&s->lock);
    AllocHandle h = mslab_alloc(s->slab, size);
    pthread_mutex_unlock(&s->lock);
    return h;
}

void*
mutex_get(MutexSlab* s, AllocHandle h)
{
    pthread_mutex_lock(&s->lock);
    void* ptr = mslab_get(s->slab, h);
    pthread_mutex_unlock(&s->lock);
    return ptr;
}

static void 
tls_destructor(void* ptr)
{
    (void)ptr;
}

TLSSlab*
tls_create(void)
{
    TLSSlab* s = (TLSSlab*)calloc(1, sizeof(TLSSlab));
    pthread_key_create(&s->key, tls_destructor);
    pthread_mutex_init(&s->reg_lock, NULL);
    s->n_registered = 0;
    return s;
}

void
tls_destroy(TLSSlab* s)
{
    for (int i = 0; i < s->n_registered; i++)
        if (s->per_thread[i])
            mslab_destroy(s->per_thread[i]);
    pthread_key_delete(s->key);
    pthread_mutex_destroy(&s->reg_lock);
    free(s);
}

void
tls_register_thread(TLSSlab* s, int thread_id)
{
    MultiSlab* my_slab = mslab_create();

    pthread_setspecific(s->key, my_slab);

    pthread_mutex_lock(&s->reg_lock);
    if (thread_id < MAX_THREADS) {
        s->per_thread[thread_id] = my_slab;
        if (thread_id >= s->n_registered)
            s->n_registered = thread_id + 1;
    }
    pthread_mutex_unlock(&s->reg_lock);
}

AllocHandle
tls_alloc(TLSSlab* s, size_t size)
{
    MultiSlab* my_slab = (MultiSlab*)pthread_getspecific(s->key);
    return mslab_alloc(my_slab, size);
}

void*
tls_get(TLSSlab* s, AllocHandle h) {
    MultiSlab* my_slab = (MultiSlab*)pthread_getspecific(s->key);
    return mslab_get(my_slab, h);
}

void*
worker_shared(void* arg)
{
    WorkerArgs* a = (WorkerArgs*)arg;
    long sum = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < a->n_iters; i++) {
        AllocHandle h = a->hot_handles[i % a->n_hot];
        int* counter  = (int*)shared_get(a->shared, h);
        (*counter)++;
        sum += *counter;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    a->time_ms = (t1.tv_sec - t0.tv_sec) * 1e3
               + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    a->result  = sum;
    return NULL;
}

void*
worker_mutex(void* arg)
{
    WorkerArgs* a = (WorkerArgs*)arg;
    long sum = 0;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < a->n_iters; i++) {
        AllocHandle h = a->hot_handles[i % a->n_hot];
        int* counter  = (int*)mutex_get(a->mutex, h);
        (*counter)++;
        sum += *counter;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    a->time_ms = (t1.tv_sec - t0.tv_sec) * 1e3
               + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    a->result  = sum;
    return NULL;
}

void*
worker_tls(void* arg)
{
    WorkerArgs* a = (WorkerArgs*)arg;

    tls_register_thread(a->tls, a->thread_id);

    AllocHandle* my_handles =
        (AllocHandle*)malloc(sizeof(AllocHandle) * (size_t)a->n_hot);
    for (int i = 0; i < a->n_hot; i++) {
        my_handles[i] = tls_alloc(a->tls, sizeof(int) * 4);
        int* p = (int*)tls_get(a->tls, my_handles[i]);
        *p = 0;
    }

    long sum = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < a->n_iters; i++) {
        AllocHandle h = my_handles[i % a->n_hot];
        int* counter  = (int*)tls_get(a->tls, h);
        (*counter)++;
        sum += *counter;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    a->time_ms = (t1.tv_sec - t0.tv_sec) * 1e3
               + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    a->result  = sum;
    free(my_handles);
    return NULL;
}

#endif ///__THREAD_SLAB_C__

