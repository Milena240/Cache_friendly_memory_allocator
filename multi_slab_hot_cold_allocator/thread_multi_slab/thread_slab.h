#ifndef __THREAD_SLAB_H__
#define __THREAD_SLAB_H__

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "../multi_slab/multi_slab.h"

#define MAX_THREADS 8
#define PAD_SLOT  64
#define PAD_CAP   16384


///Unsafe
typedef struct {
    MultiSlab* slab;
} SharedSlab;

SharedSlab* shared_create (void);
void        shared_destroy(SharedSlab* s);
AllocHandle shared_alloc  (SharedSlab* s, size_t size);
void*       shared_get    (SharedSlab* s, AllocHandle h);

///Global Mutex
typedef struct {
    MultiSlab*      slab;
    pthread_mutex_t lock;
} MutexSlab;

MutexSlab*  mutex_create (void);
void        mutex_destroy(MutexSlab* s);
AllocHandle mutex_alloc  (MutexSlab* s, size_t size);
void*       mutex_get    (MutexSlab* s, AllocHandle h);

///Thread_local
typedef struct {
    MultiSlab*      per_thread[MAX_THREADS];
    int             n_registered;
    pthread_key_t   key;
    pthread_mutex_t reg_lock;  
} TLSSlab;

TLSSlab*    tls_create            (void);
void        tls_destroy           (TLSSlab* s);
void        tls_register_thread   (TLSSlab* s, int thread_id);
AllocHandle tls_alloc             (TLSSlab* s, size_t size);
void*       tls_get               (TLSSlab* s, AllocHandle h);

typedef struct {
    int   thread_id;
    int   n_iters;
    int   n_hot;

    SharedSlab*  shared;
    MutexSlab*   mutex;
    TLSSlab*     tls;

    AllocHandle* hot_handles;
    double time_ms;
    long   result;
} WorkerArgs;

void* worker_shared (void* arg);
void* worker_mutex  (void* arg);
void* worker_tls    (void* arg);

#endif ///__THREAD_SLAB_H__

