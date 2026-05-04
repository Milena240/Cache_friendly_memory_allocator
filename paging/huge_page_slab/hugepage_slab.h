#ifndef HUGEPAGE_SLAB_H
#define HUGEPAGE_SLAB_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

#ifndef HP_SLAB_HOT_PERCENTILE
#  define HP_SLAB_HOT_PERCENTILE   0.20
#endif

#ifndef HP_SLAB_EPOCH_SIZE
#  define HP_SLAB_EPOCH_SIZE       8000
#endif

#define HUGE_PAGE_SIZE   (2UL * 1024 * 1024)

#ifndef MADV_WILLNEED
#  define MADV_WILLNEED  3
#endif
#ifndef MADV_DONTNEED
#  define MADV_DONTNEED  4
#endif
#ifndef MADV_NORMAL
#  define MADV_NORMAL    0
#endif
#ifndef MADV_COLD
#  define MADV_COLD     20
#endif
#ifndef MADV_HUGEPAGE
#  define MADV_HUGEPAGE 14
#endif
#ifndef MADV_NOHUGEPAGE
#  define MADV_NOHUGEPAGE 15
#endif

#ifndef MAP_HUGETLB
#  define MAP_HUGETLB 0x40000
#endif

typedef int HpSlabHandle;
#define HP_SLAB_INVALID  (-1)

typedef enum { HP_FREE = 0, HP_USED = 1 } HPSlotState;

typedef enum {
    HP_REGION_HOT  = 0,
    HP_REGION_COLD = 1
} HPRegion;

typedef struct {
    HPSlotState    state;
    HPRegion       region;
    HpSlabHandle   handle;
    long long      access_count;
    long long      total_count;
    int            cold_epochs;
    uintptr_t      last_phys_page;
    int            page_migrations;
} HPSlotMeta;

typedef struct {
    char*  base;
    size_t byte_size;
    int    capacity;
    int    n_used;
    int    is_locked;
    int    uses_huge_pages;
} HPRegionDesc;

typedef struct {
    size_t obj_size;
    size_t page_size;
    size_t huge_page_size;
    int    hot_capacity;
    int    cold_capacity;
    int    total_capacity;

    HPRegionDesc hot;
    HPRegionDesc cold;

    HPSlotMeta* meta;

    int* handle_to_slot;
    int* slot_to_handle;
    int  n_handles;

    int* hot_free;
    int  hot_free_top;
    int* cold_free;
    int  cold_free_top;

    long long epoch_accesses;
    int       epoch_size;
    int       n_epochs;
    int       n_compactions;
    int       n_promotions;
    int       n_demotions;
    int       n_hot_slots;
    int       n_cold_slots;

    int madvise_cold_works;
    int mlock_works;
    int huge_pages_hot;
    int huge_pages_cold;

    int pagemap_fd;
} HpAwareSlab;

HpAwareSlab*  hp_slab_create (size_t obj_size, int capacity);
void          hp_slab_destroy(HpAwareSlab* s);

HpSlabHandle  hp_slab_alloc(HpAwareSlab* s);
void          hp_slab_free (HpAwareSlab* s, HpSlabHandle h);

void* hp_slab_get    (HpAwareSlab* s, HpSlabHandle h);
void* hp_slab_get_raw(HpAwareSlab* s, HpSlabHandle h);

void hp_slab_epoch_check(HpAwareSlab* s);
void hp_slab_compact    (HpAwareSlab* s);
void hp_slab_advise     (HpAwareSlab* s);

uintptr_t hp_slab_virt_to_pfn  (HpAwareSlab* s, void* vaddr);
void      hp_slab_snapshot_pages(HpAwareSlab* s);
void      hp_slab_diff_pages    (HpAwareSlab* s, const char* label);

void hp_slab_print_layout   (HpAwareSlab* s, int show_n);
void hp_slab_print_stats    (HpAwareSlab* s, const char* label);
void hp_slab_print_page_map (HpAwareSlab* s, int show_n);

static inline void* hp_slab_slot_ptr(HpAwareSlab* s, int gslot)
{
    if (gslot < s->hot_capacity)
        return s->hot.base  + (size_t)gslot * s->obj_size;
    else
        return s->cold.base + (size_t)(gslot - s->hot_capacity) * s->obj_size;
}

#endif /* HUGEPAGE_SLAB_H */
