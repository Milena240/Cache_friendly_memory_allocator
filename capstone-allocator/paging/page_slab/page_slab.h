#ifndef PAGE_SLAB_H
#define PAGE_SLAB_H

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#ifndef PAGE_SLAB_HOT_PERCENTILE
#  define PAGE_SLAB_HOT_PERCENTILE   0.20
#endif

#ifndef PAGE_SLAB_EPOCH_SIZE
#  define PAGE_SLAB_EPOCH_SIZE       8000
#endif

#ifndef MADV_COLD
#  define MADV_COLD    20
#endif
#ifndef MADV_PAGEOUT
#  define MADV_PAGEOUT 21
#endif


typedef int PageSlabHandle;
#define PAGE_SLAB_INVALID  (-1)

typedef enum { PS_FREE = 0, PS_USED = 1 } PSSlotState;

typedef enum {
    PS_REGION_HOT  = 0,
    PS_REGION_COLD = 1
} PSRegion;


typedef struct {
    PSSlotState    state;
    PSRegion       region;
    PageSlabHandle handle;
    long long      access_count;
    long long      total_count;
    int            cold_epochs;
} PSSlotMeta;

typedef struct {
    char*  base;
    size_t byte_size;
    int    capacity;
    int    n_used;
    int    is_locked;
} PSRegionDesc;

typedef struct {
    size_t obj_size;
    size_t page_size;
    int    hot_capacity;
    int    cold_capacity;

    PSRegionDesc hot;
    PSRegionDesc cold;

    PSSlotMeta* meta;
    int         total_capacity;

    int* handle_to_slot;
    int* slot_to_handle;
    int  n_handles;

    int* hot_free;
    int  hot_free_top;
    int* cold_free;
    int  cold_free_top;

    long long epoch_accesses;
    int       n_epochs;
    int       n_compactions;
    int       n_promotions;
    int       n_demotions;

    int n_hot_slots;
    int n_cold_slots;

    int madvise_cold_works;
    int mlock_works;
} PageAwareSlab;


PageAwareSlab* page_slab_create (size_t obj_size, int capacity);
void           page_slab_destroy(PageAwareSlab* s);

PageSlabHandle page_slab_alloc(PageAwareSlab* s);
void           page_slab_free (PageAwareSlab* s, PageSlabHandle h);

void* page_slab_get    (PageAwareSlab* s, PageSlabHandle h);
void* page_slab_get_raw(PageAwareSlab* s, PageSlabHandle h);

void page_slab_epoch_check(PageAwareSlab* s);
void page_slab_compact    (PageAwareSlab* s);

void page_slab_advise(PageAwareSlab* s);

void page_slab_print_layout(PageAwareSlab* s, int show_n);
void page_slab_print_stats (PageAwareSlab* s, const char* label);

static inline void* page_slab_slot_ptr(PageAwareSlab* s, int gslot)
{
    if (gslot < s->hot_capacity)
        return s->hot.base  + (size_t)gslot * s->obj_size;
    else
        return s->cold.base + (size_t)(gslot - s->hot_capacity) * s->obj_size;
}

#endif /* PAGE_SLAB_H */
