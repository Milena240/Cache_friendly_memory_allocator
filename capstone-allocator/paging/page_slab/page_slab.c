#define _GNU_SOURCE
#include "page_slab.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

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
#ifndef MADV_SEQUENTIAL
#  define MADV_SEQUENTIAL 2
#endif
#ifndef MADV_PAGEOUT
#  define MADV_PAGEOUT  21
#endif

static inline size_t page_round_up(size_t bytes, size_t page_size)
{
    return (bytes + page_size - 1) & ~(page_size - 1);
}

static int probe_madv_cold(void* page, size_t page_size)
{
    int r = madvise(page, page_size, MADV_COLD);
    return (r == 0);
}

static void advise_range(void* ptr, size_t len, int advice)
{
    if (len == 0) return;
    madvise(ptr, len, advice);
}

PageAwareSlab*
page_slab_create(size_t obj_size, int capacity)
{
    PageAwareSlab* s = (PageAwareSlab*)calloc(1, sizeof(PageAwareSlab));
    if (!s) return NULL;

    s->page_size = (size_t)getpagesize();
    s->obj_size  = obj_size;

    int n_hot  = (int)(capacity * PAGE_SLAB_HOT_PERCENTILE);
    if (n_hot < 1) n_hot = 1;
    int n_cold = capacity;

    s->hot_capacity   = n_hot;
    s->cold_capacity  = n_cold;
    s->total_capacity = n_hot + n_cold;

    size_t hot_bytes  = page_round_up((size_t)n_hot  * obj_size, s->page_size);
    size_t cold_bytes = page_round_up((size_t)n_cold * obj_size, s->page_size);

    void* hot_ptr = mmap(NULL, hot_bytes,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (hot_ptr == MAP_FAILED) {
        perror("page_slab_create: mmap hot");
        free(s); return NULL;
    }

    void* cold_ptr = mmap(NULL, cold_bytes,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (cold_ptr == MAP_FAILED) {
        perror("page_slab_create: mmap cold");
        munmap(hot_ptr, hot_bytes);
        free(s); return NULL;
    }

    s->hot.base      = (char*)hot_ptr;
    s->hot.byte_size = hot_bytes;
    s->hot.capacity  = n_hot;

    s->cold.base      = (char*)cold_ptr;
    s->cold.byte_size = cold_bytes;
    s->cold.capacity  = n_cold;

    if (mlock(s->hot.base, s->hot.byte_size) == 0) {
        s->hot.is_locked = 1;
        s->mlock_works   = 1;
    } else {
        s->hot.is_locked = 0;
        s->mlock_works   = 0;
    }

    advise_range(s->hot.base,  s->hot.byte_size,  MADV_WILLNEED);
    advise_range(s->hot.base,  s->hot.byte_size,  MADV_SEQUENTIAL);
    advise_range(s->cold.base, s->cold.byte_size, MADV_NORMAL);

    s->madvise_cold_works = probe_madv_cold(s->cold.base, s->page_size);

    s->meta = (PSSlotMeta*)calloc((size_t)s->total_capacity, sizeof(PSSlotMeta));
    for (int i = 0; i < s->total_capacity; i++) {
        s->meta[i].state  = PS_FREE;
        s->meta[i].region = (i < n_hot) ? PS_REGION_HOT : PS_REGION_COLD;
        s->meta[i].handle = PAGE_SLAB_INVALID;
    }

    s->handle_to_slot = (int*)malloc(sizeof(int) * (size_t)s->total_capacity);
    s->slot_to_handle = (int*)malloc(sizeof(int) * (size_t)s->total_capacity);
    for (int i = 0; i < s->total_capacity; i++) {
        s->handle_to_slot[i] = PAGE_SLAB_INVALID;
        s->slot_to_handle[i] = PAGE_SLAB_INVALID;
    }
    s->n_handles = 0;

    s->hot_free  = (int*)malloc(sizeof(int) * (size_t)n_hot);
    s->cold_free = (int*)malloc(sizeof(int) * (size_t)n_cold);

    s->hot_free_top = 0;
    for (int i = n_hot - 1; i >= 0; i--)
        s->hot_free[s->hot_free_top++] = i;

    s->cold_free_top = 0;
    for (int i = s->total_capacity - 1; i >= n_hot; i--)
        s->cold_free[s->cold_free_top++] = i;

    return s;
}

void
page_slab_destroy(PageAwareSlab* s)
{
    if (!s) return;

    if (s->hot.is_locked)
        munlock(s->hot.base, s->hot.byte_size);

    munmap(s->hot.base,  s->hot.byte_size);
    munmap(s->cold.base, s->cold.byte_size);

    free(s->meta);
    free(s->handle_to_slot);
    free(s->slot_to_handle);
    free(s->hot_free);
    free(s->cold_free);
    free(s);
}

PageSlabHandle
page_slab_alloc(PageAwareSlab* s)
{
    if (s->cold_free_top == 0) return PAGE_SLAB_INVALID;

    int gslot = s->cold_free[--s->cold_free_top];
    PageSlabHandle h = s->n_handles++;

    s->handle_to_slot[h]     = gslot;
    s->slot_to_handle[gslot] = h;

    PSSlotMeta* m = &s->meta[gslot];
    m->state        = PS_USED;
    m->region       = PS_REGION_COLD;
    m->handle       = h;
    m->access_count = 0;
    m->total_count  = 0;
    m->cold_epochs  = 0;

    memset(page_slab_slot_ptr(s, gslot), 0, s->obj_size);
    s->cold.n_used++;
    s->n_cold_slots++;
    return h;
}

void
page_slab_free(PageAwareSlab* s, PageSlabHandle h)
{
    if (h < 0 || h >= s->n_handles) return;
    int gslot = s->handle_to_slot[h];
    if (gslot == PAGE_SLAB_INVALID) return;

    PSSlotMeta* m = &s->meta[gslot];
    PSRegion region = m->region;

    m->state  = PS_FREE;
    m->handle = PAGE_SLAB_INVALID;
    s->handle_to_slot[h]     = PAGE_SLAB_INVALID;
    s->slot_to_handle[gslot] = PAGE_SLAB_INVALID;

    if (region == PS_REGION_HOT) {
        s->hot_free[s->hot_free_top++] = gslot;
        s->hot.n_used--;
        s->n_hot_slots--;
    } else {
        s->cold_free[s->cold_free_top++] = gslot;
        s->cold.n_used--;
        s->n_cold_slots--;
    }
}

void*
page_slab_get(PageAwareSlab* s, PageSlabHandle h)
{
    int gslot = s->handle_to_slot[h];
    if (gslot == PAGE_SLAB_INVALID) return NULL;

    PSSlotMeta* m = &s->meta[gslot];
    m->access_count++;
    m->total_count++;
    m->cold_epochs = 0;
    s->epoch_accesses++;

    if (s->epoch_accesses >= PAGE_SLAB_EPOCH_SIZE) {
        page_slab_epoch_check(s);
        gslot = s->handle_to_slot[h];
        if (gslot == PAGE_SLAB_INVALID) return NULL;
    }

    return page_slab_slot_ptr(s, gslot);
}

void*
page_slab_get_raw(PageAwareSlab* s, PageSlabHandle h)
{
    int gslot = s->handle_to_slot[h];
    if (gslot == PAGE_SLAB_INVALID) return NULL;
    return page_slab_slot_ptr(s, gslot);
}

static void
migrate_slot(PageAwareSlab* s, int src_gslot, int dst_gslot)
{
    if (src_gslot == dst_gslot) return;

    void* src_ptr = page_slab_slot_ptr(s, src_gslot);
    void* dst_ptr = page_slab_slot_ptr(s, dst_gslot);
    memcpy(dst_ptr, src_ptr, s->obj_size);
    memset(src_ptr, 0, s->obj_size);

    PSSlotMeta* sm = &s->meta[src_gslot];
    PSSlotMeta* dm = &s->meta[dst_gslot];

    *dm = *sm;
    dm->region = (dst_gslot < s->hot_capacity)
                 ? PS_REGION_HOT : PS_REGION_COLD;

    sm->state  = PS_FREE;
    sm->handle = PAGE_SLAB_INVALID;

    PageSlabHandle h = dm->handle;
    s->handle_to_slot[h]         = dst_gslot;
    s->slot_to_handle[dst_gslot] = h;
    s->slot_to_handle[src_gslot] = PAGE_SLAB_INVALID;
}

typedef struct { int gslot; long long count; } GSlotCount;

static int cmp_gslot_desc(const void* a, const void* b)
{
    const GSlotCount* sa = (const GSlotCount*)a;
    const GSlotCount* sb = (const GSlotCount*)b;
    if (sb->count > sa->count) return  1;
    if (sb->count < sa->count) return -1;
    return 0;
}

void
page_slab_compact(PageAwareSlab* s)
{
    int total_used = s->hot.n_used + s->cold.n_used;
    if (total_used == 0) return;

    GSlotCount* sc = (GSlotCount*)malloc(
                         sizeof(GSlotCount) * (size_t)total_used);
    int n = 0;
    for (int i = 0; i < s->total_capacity; i++) {
        if (s->meta[i].state == PS_USED) {
            sc[n].gslot = i;
            sc[n].count = s->meta[i].access_count;
            n++;
        }
    }

    qsort(sc, (size_t)n, sizeof(GSlotCount), cmp_gslot_desc);

    int n_should_be_hot = (int)(n * PAGE_SLAB_HOT_PERCENTILE);
    if (n_should_be_hot < 1 && n > 0) n_should_be_hot = 1;

    for (int rank = 0; rank < n; rank++) {
        int gslot = sc[rank].gslot;
        PSSlotMeta* m = &s->meta[gslot];

        int should_be_hot = (rank < n_should_be_hot);
        int is_hot        = (m->region == PS_REGION_HOT);

        if (should_be_hot && !is_hot) {
            if (s->hot_free_top > 0) {
                int free_hot = s->hot_free[--s->hot_free_top];
                migrate_slot(s, gslot, free_hot);
                s->cold_free[s->cold_free_top++] = gslot;
                s->cold.n_used--;
                s->hot.n_used++;
                s->n_promotions++;
            }
        } else if (!should_be_hot && is_hot) {
            if (s->cold_free_top > 0) {
                m->cold_epochs++;
                int free_cold = s->cold_free[--s->cold_free_top];
                migrate_slot(s, gslot, free_cold);
                s->hot_free[s->hot_free_top++] = gslot;
                s->hot.n_used--;
                s->cold.n_used++;
                s->n_demotions++;
            }
        }
    }

    s->n_hot_slots  = 0;
    s->n_cold_slots = 0;
    for (int i = 0; i < s->total_capacity; i++) {
        if (s->meta[i].state != PS_USED) continue;
        if (s->meta[i].region == PS_REGION_HOT) s->n_hot_slots++;
        else                                     s->n_cold_slots++;
    }

    for (int i = 0; i < s->total_capacity; i++)
        if (s->meta[i].state == PS_USED)
            s->meta[i].access_count = 0;

    page_slab_advise(s);

    s->n_compactions++;
    free(sc);
}

void
page_slab_advise(PageAwareSlab* s)
{
    if (s->hot.n_used > 0) {
        size_t used_bytes = page_round_up(
            (size_t)s->hot.n_used * s->obj_size, s->page_size);
        if (used_bytes > s->hot.byte_size) used_bytes = s->hot.byte_size;
        advise_range(s->hot.base, used_bytes, MADV_WILLNEED);
    }

    if (s->cold.n_used > 0) {
        size_t used_bytes = page_round_up(
            (size_t)s->cold.n_used * s->obj_size, s->page_size);
        if (used_bytes > s->cold.byte_size) used_bytes = s->cold.byte_size;

        if (s->madvise_cold_works)
            advise_range(s->cold.base, used_bytes, MADV_COLD);
        else
            advise_range(s->cold.base, used_bytes, MADV_NORMAL);
    }
}

void
page_slab_epoch_check(PageAwareSlab* s)
{
    s->n_epochs++;
    s->epoch_accesses = 0;

    for (int i = 0; i < s->total_capacity; i++)
        if (s->meta[i].state == PS_USED && s->meta[i].access_count == 0)
            s->meta[i].cold_epochs++;

    page_slab_compact(s);
}

void
page_slab_print_stats(PageAwareSlab* s, const char* label)
{
    printf("  ── PageAwareSlab stats: %s ──\n", label);
    printf("  Object size:       %zu bytes\n", s->obj_size);
    printf("  Page size:         %zu bytes\n", s->page_size);
    printf("  Hot  capacity:     %d slots  (%zu KB, %s)\n",
           s->hot_capacity,
           s->hot.byte_size / 1024,
           s->hot.is_locked ? "mlock'd" : "not locked");
    printf("  Cold capacity:     %d slots  (%zu KB)\n",
           s->cold_capacity,
           s->cold.byte_size / 1024);
    printf("  Hot  used:         %d\n",  s->n_hot_slots);
    printf("  Cold used:         %d\n",  s->n_cold_slots);
    printf("  Epochs:            %d\n",  s->n_epochs);
    printf("  Compactions:       %d\n",  s->n_compactions);
    printf("  Promotions (c→h):  %d\n",  s->n_promotions);
    printf("  Demotions  (h→c):  %d\n",  s->n_demotions);
    printf("  MADV_COLD works:   %s\n",  s->madvise_cold_works ? "yes" : "no (fallback: MADV_NORMAL — safe on any kernel)");
    printf("  mlock works:       %s\n",  s->mlock_works        ? "yes" : "no");
    printf("\n");
}

void
page_slab_print_layout(PageAwareSlab* s, int show_n)
{
    printf("  Slot layout (first %d global slots):\n\n", show_n);
    printf("  %-6s  %-8s  %-12s  %-10s  %-8s  %s\n",
           "GSlot", "Handle", "Region", "Zone", "Accesses", "ColdEpochs");
    printf("  %-6s  %-8s  %-12s  %-10s  %-8s  %s\n",
           "─────", "──────", "──────", "────", "────────", "──────────");

    int cap = s->total_capacity;
    if (show_n < cap) cap = show_n;

    for (int i = 0; i < cap; i++) {
        PSSlotMeta* m = &s->meta[i];
        if (m->state == PS_FREE) {
            printf("  %-6d  %-8s  %-12s\n", i, "free", "-");
            continue;
        }
        const char* reg  = (m->region == PS_REGION_HOT)  ? "HOT" : "cold";
        printf("  %-6d  %-8d  %-12s  %-10lld  %-8lld  %d\n",
               i, m->handle, reg,
               m->access_count, m->total_count, m->cold_epochs);
    }
    printf("\n");
}
