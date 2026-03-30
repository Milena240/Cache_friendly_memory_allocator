#ifndef MULTI_SLAB_H
#define MULTI_SLAB_H

#include <stddef.h>
#include <stdint.h>
#include "../hot_cold_slab_allocator/hot_cold_slab.h"

#define N_SIZE_CLASSES   5
#define MAX_SLAB_SIZE  256  

static const size_t SIZE_CLASS_BYTES[N_SIZE_CLASSES] = {
    16, 32, 64, 128, 256
};

static const int SIZE_CLASS_CAPACITY[N_SIZE_CLASSES] = {
    65536,  
    32768,  
    16384,  
     8192,  
     4096   
};

typedef struct {
    int slab_index;        
    int slot_handle;  
    void* large_ptr;  
} AllocHandle;

static const AllocHandle NULL_HANDLE = { -2, -1, NULL };

static inline int alloc_handle_valid(AllocHandle h) {
    return h.slab_index != -2;
}

typedef struct {
    HotColdSlab* slabs[N_SIZE_CLASSES];  

    long long total_allocs;
    long long total_frees;
    long long large_allocs;   

    long long class_allocs[N_SIZE_CLASSES];
} MultiSlab;


MultiSlab*  mslab_create (void);
void        mslab_destroy(MultiSlab* m);

AllocHandle mslab_alloc  (MultiSlab* m, size_t size);
void        mslab_free   (MultiSlab* m, AllocHandle h);
void*       mslab_get    (MultiSlab* m, AllocHandle h);
void*       mslab_get_raw(MultiSlab* m, AllocHandle h);
void        mslab_compact_all(MultiSlab* m);
void        mslab_compact_class(MultiSlab* m, int class_index);

void mslab_print_summary    (MultiSlab* m);
void mslab_print_class_stats(MultiSlab* m, int class_index);
void mslab_print_all_classes(MultiSlab* m);
int  mslab_class_for_size   (size_t size);
int  mslab_objects_per_cache_line(int class_index);

#endif /* MULTI_SLAB_H */
