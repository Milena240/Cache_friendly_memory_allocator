#include "hugepage_slab.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

static inline size_t round_up(size_t bytes, size_t align)
{
    return (bytes + align - 1) & ~(align - 1);
}

static void advise_range(void* ptr, size_t len, int advice)
{
    if (len == 0) return;
    madvise(ptr, len, advice);
}

static void* alloc_huge_region(size_t bytes, int* used_huge_pages)
{
    size_t aligned = round_up(bytes, HUGE_PAGE_SIZE);

    void* ptr = mmap(NULL, aligned,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);

    if (ptr != MAP_FAILED) {
        *used_huge_pages = 1;
        return ptr;
    }

    ptr = mmap(NULL, aligned,
               PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS,
               -1, 0);

    if (ptr == MAP_FAILED) return NULL;

    madvise(ptr, aligned, MADV_HUGEPAGE);
    advise_range(ptr, aligned, MADV_WILLNEED);

    *used_huge_pages = 0;
    return ptr;
}

HpAwareSlab*
hp_slab_create(size_t obj_size, int capacity)
{
    HpAwareSlab* s = (HpAwareSlab*)calloc(1, sizeof(HpAwareSlab));
    if (!s) return NULL;

    s->page_size       = (size_t)getpagesize();
    s->huge_page_size  = HUGE_PAGE_SIZE;
    s->obj_size        = obj_size;

    int n_hot  = (int)(capacity * HP_SLAB_HOT_PERCENTILE);
    if (n_hot < 1) n_hot = 1;
    int n_cold = capacity;

    s->hot_capacity   = n_hot;
    s->cold_capacity  = n_cold;
    s->total_capacity = n_hot + n_cold;

    size_t hot_bytes  = round_up((size_t)n_hot  * obj_size, HUGE_PAGE_SIZE);
    size_t cold_bytes = round_up((size_t)n_cold * obj_size, HUGE_PAGE_SIZE);

    int hot_huge = 0;
    void* hot_ptr = alloc_huge_region(hot_bytes, &hot_huge);
    if (!hot_ptr) {
        perror("hp_slab_create: alloc hot region");
        free(s); return NULL;
    }

    int cold_huge = 0;
    void* cold_ptr = alloc_huge_region(cold_bytes, &cold_huge);
    if (!cold_ptr) {
        perror("hp_slab_create: alloc cold region");
        munmap(hot_ptr, hot_bytes);
        free(s); return NULL;
    }

    s->hot.base            = (char*)hot_ptr;
    s->hot.byte_size       = hot_bytes;
    s->hot.capacity        = n_hot;
    s->hot.uses_huge_pages = hot_huge;

    s->cold.base            = (char*)cold_ptr;
    s->cold.byte_size       = cold_bytes;
    s->cold.capacity        = n_cold;
    s->cold.uses_huge_pages = cold_huge;

    s->huge_pages_hot  = hot_huge;
    s->huge_pages_cold = cold_huge;

    if (mlock(s->hot.base, s->hot.byte_size) == 0) {
        s->hot.is_locked = 1;
        s->mlock_works   = 1;
    }

    advise_range(s->hot.base,  s->hot.byte_size,  MADV_WILLNEED);
    advise_range(s->cold.base, s->cold.byte_size, MADV_NORMAL);

    {
        int r = madvise(s->cold.base, s->page_size, MADV_COLD);
        s->madvise_cold_works = (r == 0);
    }

    s->meta = (HPSlotMeta*)calloc((size_t)s->total_capacity, sizeof(HPSlotMeta));
    if (!s->meta) { hp_slab_destroy(s); return NULL; }

    for (int i = 0; i < s->total_capacity; i++) {
        s->meta[i].state           = HP_FREE;
        s->meta[i].region          = (i < n_hot) ? HP_REGION_HOT : HP_REGION_COLD;
        s->meta[i].handle          = HP_SLAB_INVALID;
        s->meta[i].last_phys_page  = 0;
        s->meta[i].page_migrations = 0;
    }

    s->handle_to_slot = (int*)malloc(sizeof(int) * (size_t)s->total_capacity);
    s->slot_to_handle = (int*)malloc(sizeof(int) * (size_t)s->total_capacity);
    if (!s->handle_to_slot || !s->slot_to_handle) { hp_slab_destroy(s); return NULL; }

    for (int i = 0; i < s->total_capacity; i++) {
        s->handle_to_slot[i] = HP_SLAB_INVALID;
        s->slot_to_handle[i] = HP_SLAB_INVALID;
    }

    s->hot_free  = (int*)malloc(sizeof(int) * (size_t)n_hot);
    s->cold_free = (int*)malloc(sizeof(int) * (size_t)n_cold);
    if (!s->hot_free || !s->cold_free) { hp_slab_destroy(s); return NULL; }

    s->hot_free_top = 0;
    for (int i = n_hot - 1; i >= 0; i--)
        s->hot_free[s->hot_free_top++] = i;

    s->cold_free_top = 0;
    for (int i = s->total_capacity - 1; i >= n_hot; i--)
        s->cold_free[s->cold_free_top++] = i;

    s->epoch_size = HP_SLAB_EPOCH_SIZE;

    s->pagemap_fd = open("/proc/self/pagemap", O_RDONLY);

    return s;
}

void
hp_slab_destroy(HpAwareSlab* s)
{
    if (!s) return;
    if (s->hot.base) {
        if (s->hot.is_locked)
            munlock(s->hot.base, s->hot.byte_size);
        munmap(s->hot.base, s->hot.byte_size);
    }
    if (s->cold.base)
        munmap(s->cold.base, s->cold.byte_size);

    if (s->pagemap_fd >= 0) {
        close(s->pagemap_fd);
        s->pagemap_fd = -1;
    }
    free(s->meta);
    free(s->handle_to_slot);
    free(s->slot_to_handle);
    free(s->hot_free);
    free(s->cold_free);
    free(s);
}

HpSlabHandle
hp_slab_alloc(HpAwareSlab* s)
{
    if (s->cold_free_top == 0) return HP_SLAB_INVALID;

    int gslot = s->cold_free[--s->cold_free_top];
    HpSlabHandle h = s->n_handles++;

    s->handle_to_slot[h]     = gslot;
    s->slot_to_handle[gslot] = h;

    HPSlotMeta* m = &s->meta[gslot];
    m->state        = HP_USED;
    m->region       = HP_REGION_COLD;
    m->handle       = h;
    m->access_count = 0;
    m->total_count  = 0;
    m->cold_epochs  = 0;

    memset(hp_slab_slot_ptr(s, gslot), 0, s->obj_size);
    s->cold.n_used++;
    s->n_cold_slots++;
    return h;
}

void
hp_slab_free(HpAwareSlab* s, HpSlabHandle h)
{
    if (h < 0 || h >= s->n_handles) return;
    int gslot = s->handle_to_slot[h];
    if (gslot == HP_SLAB_INVALID) return;

    HPSlotMeta* m   = &s->meta[gslot];
    HPRegion region = m->region;

    m->state  = HP_FREE;
    m->handle = HP_SLAB_INVALID;
    s->handle_to_slot[h]     = HP_SLAB_INVALID;
    s->slot_to_handle[gslot] = HP_SLAB_INVALID;

    if (region == HP_REGION_HOT) {
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
hp_slab_get(HpAwareSlab* s, HpSlabHandle h)
{
    int gslot = s->handle_to_slot[h];
    if (gslot == HP_SLAB_INVALID) return NULL;

    HPSlotMeta* m = &s->meta[gslot];
    m->access_count++;
    m->total_count++;
    m->cold_epochs = 0;
    s->epoch_accesses++;

    if (s->epoch_accesses >= s->epoch_size) {
        hp_slab_epoch_check(s);
        gslot = s->handle_to_slot[h];
        if (gslot == HP_SLAB_INVALID) return NULL;
    }

    return hp_slab_slot_ptr(s, gslot);
}

void*
hp_slab_get_raw(HpAwareSlab* s, HpSlabHandle h)
{
    int gslot = s->handle_to_slot[h];
    if (gslot == HP_SLAB_INVALID) return NULL;
    return hp_slab_slot_ptr(s, gslot);
}

uintptr_t
hp_slab_virt_to_pfn(HpAwareSlab* s, void* vaddr)
{
    if (s->pagemap_fd < 0) return 0;

    uintptr_t vpage  = (uintptr_t)vaddr / s->page_size;
    off_t     offset = (off_t)(vpage * sizeof(uint64_t));

    uint64_t entry = 0;
    if (pread(s->pagemap_fd, &entry, sizeof(entry), offset) != sizeof(entry))
        return 0;

    if (!(entry & (1ULL << 63))) return 0;

    uintptr_t pfn = entry & 0x7fffffffffffffULL;
    return pfn * s->page_size;
}

void
hp_slab_snapshot_pages(HpAwareSlab* s)
{
    for (int i = 0; i < s->total_capacity; i++) {
        HPSlotMeta* m = &s->meta[i];
        if (m->state != HP_USED) continue;
        void* ptr = hp_slab_slot_ptr(s, i);
        m->last_phys_page = hp_slab_virt_to_pfn(s, ptr);
    }
}

void
hp_slab_diff_pages(HpAwareSlab* s, const char* label)
{
    printf("page migration diff: %s\n", label);
    int moved = 0, total = 0;

    for (int i = 0; i < s->total_capacity; i++) {
        HPSlotMeta* m = &s->meta[i];
        if (m->state != HP_USED) continue;
        total++;

        void*     ptr      = hp_slab_slot_ptr(s, i);
        uintptr_t cur_phys = hp_slab_virt_to_pfn(s, ptr);

        if (cur_phys == 0 && m->last_phys_page == 0) continue;

        int changed = (cur_phys != m->last_phys_page && m->last_phys_page != 0);
        if (changed) {
            m->page_migrations++;
            moved++;
            printf("  gslot %3d  handle %3d  %-5s  phys: 0x%012lx -> 0x%012lx  [MOVED]\n",
                   i, m->handle,
                   m->region == HP_REGION_HOT ? "HOT" : "cold",
                   (unsigned long)m->last_phys_page,
                   (unsigned long)cur_phys);
        }
        m->last_phys_page = cur_phys;
    }

    if (moved == 0)
        printf("  no physical page changes detected among %d used slots.\n", total);
    else
        printf("  %d / %d slots moved to a different physical page.\n", moved, total);
    printf("\n");
}

static void
migrate_slot(HpAwareSlab* s, int src_gslot, int dst_gslot)
{
    if (src_gslot == dst_gslot) return;

    void* src_ptr = hp_slab_slot_ptr(s, src_gslot);
    void* dst_ptr = hp_slab_slot_ptr(s, dst_gslot);
    memcpy(dst_ptr, src_ptr, s->obj_size);
    memset(src_ptr, 0, s->obj_size);

    HPSlotMeta* sm = &s->meta[src_gslot];
    HPSlotMeta* dm = &s->meta[dst_gslot];

    *dm = *sm;
    dm->region = (dst_gslot < s->hot_capacity)
                 ? HP_REGION_HOT : HP_REGION_COLD;

    sm->state  = HP_FREE;
    sm->handle = HP_SLAB_INVALID;

    HpSlabHandle h = dm->handle;
    s->handle_to_slot[h]         = dst_gslot;
    s->slot_to_handle[dst_gslot] = h;
    s->slot_to_handle[src_gslot] = HP_SLAB_INVALID;
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
hp_slab_compact(HpAwareSlab* s)
{
    int total_used = s->hot.n_used + s->cold.n_used;
    if (total_used == 0) return;

    GSlotCount* sc = (GSlotCount*)malloc(
                         sizeof(GSlotCount) * (size_t)total_used);
    if (!sc) return;

    int n = 0;
    for (int i = 0; i < s->total_capacity; i++) {
        if (s->meta[i].state == HP_USED) {
            sc[n].gslot = i;
            sc[n].count = s->meta[i].access_count;
            n++;
        }
    }

    qsort(sc, (size_t)n, sizeof(GSlotCount), cmp_gslot_desc);

    int n_should_be_hot = (int)(n * HP_SLAB_HOT_PERCENTILE);
    if (n_should_be_hot < 1 && n > 0) n_should_be_hot = 1;

    for (int rank = 0; rank < n; rank++) {
        int gslot = sc[rank].gslot;
        HPSlotMeta* m = &s->meta[gslot];

        int should_be_hot = (rank < n_should_be_hot);
        int is_hot        = (m->region == HP_REGION_HOT);

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
        if (s->meta[i].state != HP_USED) continue;
        if (s->meta[i].region == HP_REGION_HOT) s->n_hot_slots++;
        else                                     s->n_cold_slots++;
    }

    for (int i = 0; i < s->total_capacity; i++)
        if (s->meta[i].state == HP_USED)
            s->meta[i].access_count = 0;

    hp_slab_advise(s);

    s->n_compactions++;
    free(sc);
}

void
hp_slab_advise(HpAwareSlab* s)
{
    if (s->hot.n_used > 0) {
        size_t used_bytes = round_up(
            (size_t)s->hot.n_used * s->obj_size, s->page_size);
        if (used_bytes > s->hot.byte_size) used_bytes = s->hot.byte_size;
        advise_range(s->hot.base, used_bytes, MADV_WILLNEED);
        advise_range(s->hot.base, used_bytes, MADV_HUGEPAGE);
    }

    if (s->cold.n_used > 0) {
        size_t used_bytes = round_up(
            (size_t)s->cold.n_used * s->obj_size, s->page_size);
        if (used_bytes > s->cold.byte_size) used_bytes = s->cold.byte_size;

        if (s->madvise_cold_works)
            advise_range(s->cold.base, used_bytes, MADV_COLD);
        else
            advise_range(s->cold.base, used_bytes, MADV_NORMAL);
        advise_range(s->cold.base, used_bytes, MADV_NOHUGEPAGE);
    }
}

void
hp_slab_epoch_check(HpAwareSlab* s)
{
    s->n_epochs++;
    s->epoch_accesses = 0;

    for (int i = 0; i < s->total_capacity; i++)
        if (s->meta[i].state == HP_USED && s->meta[i].access_count == 0)
            s->meta[i].cold_epochs++;

    hp_slab_compact(s);
}

void
hp_slab_print_stats(HpAwareSlab* s, const char* label)
{
    printf("stats: %s\n", label);
    printf("  object size:      %zu bytes\n",  s->obj_size);
    printf("  base page size:   %zu bytes\n",  s->page_size);
    printf("  huge page size:   %zu KB\n",     s->huge_page_size / 1024);
    printf("  hot  capacity:    %d slots  (%zu KB)  huge=%s  %s\n",
           s->hot_capacity,
           s->hot.byte_size / 1024,
           s->huge_pages_hot  ? "MAP_HUGETLB" : "THP/normal",
           s->hot.is_locked   ? "mlock'd"     : "not locked");
    printf("  cold capacity:    %d slots  (%zu KB)  huge=%s\n",
           s->cold_capacity,
           s->cold.byte_size / 1024,
           s->huge_pages_cold ? "MAP_HUGETLB" : "THP/normal");
    printf("  hot  used:        %d\n",  s->n_hot_slots);
    printf("  cold used:        %d\n",  s->n_cold_slots);
    printf("  epochs:           %d\n",  s->n_epochs);
    printf("  compactions:      %d\n",  s->n_compactions);
    printf("  promotions:       %d\n",  s->n_promotions);
    printf("  demotions:        %d\n",  s->n_demotions);
    printf("  MADV_COLD works:  %s\n",  s->madvise_cold_works ? "yes" : "no (MADV_NORMAL fallback)");
    printf("  mlock works:      %s\n",  s->mlock_works        ? "yes" : "no");
    printf("  pagemap fd:       %s\n",  s->pagemap_fd >= 0    ? "open" : "unavailable");
    printf("\n");
}

void
hp_slab_print_layout(HpAwareSlab* s, int show_n)
{
    int cap = (show_n < s->total_capacity) ? show_n : s->total_capacity;
    printf("slot layout (first %d global slots):\n\n", cap);
    printf("%-6s  %-8s  %-8s\n",
           "gslot", "handle", "region");

    for (int i = 0; i < cap; i++) {
        /* print a separator between the hot and cold regions */
        if (i == s->hot_capacity)
            printf("--- cold region ---\n");
        else if (i == 0)
            printf("--- hot  region ---\n");

        HPSlotMeta* m = &s->meta[i];
        if (m->state == HP_FREE) {
            printf("%-6d  %-8s  %-8s\n", i, "free", "-");
            continue;
        }
        const char* reg = (m->region == HP_REGION_HOT) ? "HOT" : "cold";
        printf("%-6d  %-8d  %-8s\n", i, m->handle, reg);
    }
    printf("\n");
}

void
hp_slab_print_page_map(HpAwareSlab* s, int show_n)
{
    int cap = (show_n < s->total_capacity) ? show_n : s->total_capacity;
    printf("physical page map (first %d used slots):\n\n", cap);

    if (s->pagemap_fd < 0) {
        printf("  pagemap unavailable - run as root or check /proc/sys/kernel/perf_event_paranoid\n\n");
        return;
    }

    printf("%-6s  %-8s  %-8s  %-20s  %-20s  %s\n",
           "gslot", "handle", "region", "virt addr", "phys page", "page_migr");

    int shown = 0;
    for (int i = 0; i < s->total_capacity && shown < cap; i++) {
        HPSlotMeta* m = &s->meta[i];
        if (m->state != HP_USED) continue;
        shown++;

        void*     vptr = hp_slab_slot_ptr(s, i);
        uintptr_t phys = hp_slab_virt_to_pfn(s, vptr);
        const char* reg = (m->region == HP_REGION_HOT) ? "HOT" : "cold";

        if (phys == 0)
            printf("%-6d  %-8d  %-8s  0x%016lx  %-20s  %d\n",
                   i, m->handle, reg, (unsigned long)vptr, "(not resident)", m->page_migrations);
        else
            printf("%-6d  %-8d  %-8s  0x%016lx  0x%016lx  %d\n",
                   i, m->handle, reg,
                   (unsigned long)vptr,
                   (unsigned long)phys,
                   m->page_migrations);
    }
    printf("\n");
}
