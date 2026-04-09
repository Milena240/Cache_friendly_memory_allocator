#define _POSIX_C_SOURCE 200809L
#include "hot_cold_slab.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>  

#define MMAP_THRESHOLD_BYTES  (512ULL * 1024 * 1024)

static inline void*
slot_ptr(HotColdSlab* s, int slot)
{
    return (char*)s->data + (size_t)slot * s->obj_size;
}

static void 
swap_slots(HotColdSlab* s, int slot_a, int slot_b) 
{
    if (slot_a == slot_b) return;

    char* tmp = (char*)malloc(s->obj_size);
    memcpy(tmp, slot_ptr(s, slot_a), s->obj_size);
    memcpy(slot_ptr(s, slot_a), slot_ptr(s, slot_b), s->obj_size);
    memcpy(slot_ptr(s, slot_b), tmp, s->obj_size);
    free(tmp);

    SlotMeta meta_tmp  = s->meta[slot_a];
    s->meta[slot_a]    = s->meta[slot_b];
    s->meta[slot_b]    = meta_tmp;

    int h_a = s->meta[slot_a].handle;
    int h_b = s->meta[slot_b].handle;

    s->slot_to_handle[slot_a] = h_a;
    s->slot_to_handle[slot_b] = h_b;

    if (h_a != INVALID_HANDLE) s->handle_to_slot[h_a] = slot_a;
    if (h_b != INVALID_HANDLE) s->handle_to_slot[h_b] = slot_b;
}

HotColdSlab*
slab_create(size_t obj_size, int capacity)
{
    HotColdSlab* s = (HotColdSlab*)calloc(1, sizeof(HotColdSlab));

    s->obj_size = obj_size;
    s->capacity = capacity;
    s->n_used   = 0;

    size_t data_bytes = (size_t)capacity * obj_size;
    if (data_bytes > MMAP_THRESHOLD_BYTES) {
        void* p = mmap(NULL, data_bytes,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1, 0);
        if (p == MAP_FAILED) {
            perror("slab_create: mmap");
            free(s);
            return NULL;
        }
        s->data           = (char*)p;
        s->data_is_mmap   = 1;
        s->data_mmap_size = data_bytes;
        fprintf(stderr,
            "[slab] data buffer %.1f MB → allocated via mmap\n",
            (double)data_bytes / (1024.0 * 1024.0));
    } else {
        s->data         = (char*)calloc((size_t)capacity, obj_size);
        s->data_is_mmap = 0;
    }

    s->meta = (SlotMeta*)calloc((size_t)capacity, sizeof(SlotMeta));
    for (int i = 0; i < capacity; i++) {
        s->meta[i].state  = SLOT_FREE;
        s->meta[i].zone   = ZONE_COLD;
        s->meta[i].handle = INVALID_HANDLE;
    }

    s->handle_to_slot = (int*)malloc(sizeof(int) * (size_t)capacity);
    s->slot_to_handle = (int*)malloc(sizeof(int) * (size_t)capacity);
    for (int i = 0; i < capacity; i++) {
        s->handle_to_slot[i] = INVALID_HANDLE;
        s->slot_to_handle[i] = INVALID_HANDLE;
    }
    s->n_handles = 0;

    s->free_stack = (int*)malloc(sizeof(int) * (size_t)capacity);
    s->free_top   = 0;
    for (int i = capacity - 1; i >= 0; i--)
        s->free_stack[s->free_top++] = i;

    return s;
}

void
slab_destroy(HotColdSlab* s)
{
    if (s->data_is_mmap)
        munmap(s->data, s->data_mmap_size);
    else
        free(s->data);

    free(s->meta);
    free(s->handle_to_slot);
    free(s->slot_to_handle);
    free(s->free_stack);
    free(s);
}

SlabHandle
slab_alloc(HotColdSlab* s) 
{
    if (s->free_top == 0) return INVALID_HANDLE;  
    
    int slot = s->free_stack[--s->free_top];
    
    SlabHandle h = s->n_handles++;

    s->handle_to_slot[h]    = slot;
    s->slot_to_handle[slot] = h;

    s->meta[slot].state        = SLOT_USED;
    s->meta[slot].zone         = ZONE_COLD;
    s->meta[slot].handle       = h;
    s->meta[slot].access_count = 0;
    s->meta[slot].total_count  = 0;

    memset(slot_ptr(s, slot), 0, s->obj_size);

    s->n_used++;
    return h;
}

void 
slab_free(HotColdSlab* s, SlabHandle h)
{
    if (h < 0 || h >= s->n_handles) return;

    int slot = s->handle_to_slot[h];
    if (slot == INVALID_HANDLE) return;

    s->handle_to_slot[h]    = INVALID_HANDLE;
    s->slot_to_handle[slot] = INVALID_HANDLE;
    s->meta[slot].state     = SLOT_FREE;
    s->meta[slot].handle    = INVALID_HANDLE;

    s->free_stack[s->free_top++] = slot;
    s->n_used--;
}

void*
slab_get(HotColdSlab* s, SlabHandle h)
{
    int slot = s->handle_to_slot[h];
    if (slot == INVALID_HANDLE) return NULL;

    s->meta[slot].access_count++;
    s->meta[slot].total_count++;
    s->epoch_accesses++;

    if (s->epoch_accesses >= SLAB_EPOCH_SIZE)
        slab_epoch_check(s);

    return slot_ptr(s, slot);
}

void*
slab_get_raw(HotColdSlab* s, SlabHandle h) 
{
    int slot = s->handle_to_slot[h];
    if (slot == INVALID_HANDLE) return NULL;
    return slot_ptr(s, slot);
}

void
slab_epoch_check(HotColdSlab* s) 
{
    s->n_epochs++;
    s->epoch_accesses = 0;

    slab_compact(s);

    for (int i = 0; i < s->capacity; i++)
        if (s->meta[i].state == SLOT_USED)
            s->meta[i].access_count = 0;
}


typedef struct {
    int       slot;
    long long count;
} SlotCount;

static int cmp_slot_desc(const void* a, const void* b) {
    const SlotCount* sa = (const SlotCount*)a;
    const SlotCount* sb = (const SlotCount*)b;
    if (sb->count > sa->count) return  1;
    if (sb->count < sa->count) return -1;
    return 0;
}

void slab_compact(HotColdSlab* s) {
    if (s->n_used == 0) return;

    SlotCount* counts = (SlotCount*)malloc(
                            sizeof(SlotCount) * (size_t)s->n_used);
    int n = 0;
    for (int i = 0; i < s->capacity; i++)
        if (s->meta[i].state == SLOT_USED) {
            counts[n].slot  = i;
            counts[n].count = s->meta[i].access_count;
            n++;
        }

    qsort(counts, (size_t)n, sizeof(SlotCount), cmp_slot_desc);

    int n_hot  = (int)(n * HOT_PERCENTILE);
    if (n_hot < 1 && n > 0) n_hot = 1;
    int n_warm = (int)(n * (HOT_PERCENTILE + 0.30));

    int* slot_pos = (int*)malloc(sizeof(int) * (size_t)s->capacity);
    for (int i = 0; i < s->capacity; i++) slot_pos[i] = -1;
    for (int r = 0; r < n; r++) slot_pos[counts[r].slot] = r;

    for (int target = 0; target < n; target++) {
        int current_slot = counts[target].slot;

        if (current_slot == target) continue;
        int displaced_slot = -1;
        for (int j = target + 1; j < n; j++) {
            if (counts[j].slot == target) {
                displaced_slot = j;
                break;
            }
        }

        swap_slots(s, target, current_slot);

        if (displaced_slot != -1) {
            counts[displaced_slot].slot = current_slot;
        }
        counts[target].slot = target;
    }

    int pos = 0;
    for (int i = 0; i < s->capacity; i++) {
        if (s->meta[i].state != SLOT_USED) continue;
        s->meta[i].zone = (pos < n_hot)  ? ZONE_HOT  :
                          (pos < n_warm) ? ZONE_WARM  : ZONE_COLD;
        pos++;
    }

    s->n_hot_slots  = n_hot;
    s->n_warm_slots = n_warm - n_hot;
    s->n_cold_slots = n - n_warm;
    s->n_compactions++;

    free(counts);
    free(slot_pos);
}

SlabStats
slab_get_stats(HotColdSlab* s)
{
    SlabStats st;
    st.capacity        = s->capacity;
    st.n_used          = s->n_used;
    st.n_hot           = s->n_hot_slots;
    st.n_warm          = s->n_warm_slots;
    st.n_cold          = s->n_cold_slots;
    st.n_epochs        = s->n_epochs;
    st.n_compactions   = s->n_compactions;
    st.obj_size        = s->obj_size;
    st.hot_zone_bytes  = (size_t)s->n_hot_slots * s->obj_size;
    st.hot_cache_lines = (int)((st.hot_zone_bytes + 63) / 64);
    st.hot_fraction    = s->n_used > 0
                       ? (double)s->n_hot_slots / s->n_used
                       : 0.0;
    return st;
}

void 
slab_print_stats(HotColdSlab* s, const char* label) 
{
    SlabStats st = slab_get_stats(s);
    printf("  ── Slab stats: %s ──\n", label);
    printf("  Object size:     %zu bytes\n",  st.obj_size);
    printf("  Capacity:        %d slots\n",   st.capacity);
    printf("  Used:            %d slots\n",   st.n_used);
    printf("  Epochs run:      %d\n",         st.n_epochs);
    printf("  Compactions:     %d\n",         st.n_compactions);
    printf("  Hot  slots:      %d  (%zu KB, %d cache lines)\n",
           st.n_hot, st.hot_zone_bytes/1024, st.hot_cache_lines);
    printf("  Warm slots:      %d\n",         st.n_warm);
    printf("  Cold slots:      %d\n",         st.n_cold);
    printf("\n");
}

void 
slab_print_layout(HotColdSlab* s, int show_n)
{
    printf("  Slot layout (first %d slots):\n\n", show_n);
    printf("  %-6s  %-8s  %-10s  %-6s\n",
           "Slot", "Handle", "Zone", "Accesses");
    printf("  %-6s  %-8s  %-10s  %-6s\n",
           "────", "──────", "────", "────────");

    if (show_n > s->capacity) show_n = s->capacity;
    for (int i = 0; i < show_n; i++) {
        if (s->meta[i].state == SLOT_FREE) {
            printf("  %-6d  %-8s  %-10s\n", i, "free", "-");
            continue;
        }
        const char* zone_str =
            s->meta[i].zone == ZONE_HOT  ? "HOT  ◀" :
            s->meta[i].zone == ZONE_WARM ? "warm"    : "cold";
        printf("  %-6d  %-8d  %-10s  %lld\n",
               i,
               s->meta[i].handle,
               zone_str,
               s->meta[i].total_count);
    }
    printf("\n");
}

void 
slab_print_access_report(HotColdSlab* s, int top_n)
{
    SlotCount* counts = (SlotCount*)malloc(
                            sizeof(SlotCount) * (size_t)s->n_used);
    int n = 0;
    for (int i = 0; i < s->capacity; i++)
        if (s->meta[i].state == SLOT_USED) {
            counts[n].slot  = i;
            counts[n].count = s->meta[i].total_count;
            n++;
        }
    qsort(counts, (size_t)n, sizeof(SlotCount), cmp_slot_desc);

    printf("  Access report (top %d slots by total accesses):\n\n",
           top_n);
    printf("  %-6s  %-8s  %-12s  %-6s  Zone\n",
           "Slot", "Handle", "Total acc.", "%%");
    printf("  %-6s  %-8s  %-12s  %-6s  ────\n",
           "────", "──────", "──────────", "──");

    long long total = 0;
    for (int i = 0; i < n; i++) total += counts[i].count;
    if (total == 0) total = 1;

    if (top_n > n) top_n = n;
    for (int r = 0; r < top_n; r++) {
        int slot = counts[r].slot;
        double pct = 100.0 * (double)counts[r].count / (double)total;
        const char* zone_str =
            s->meta[slot].zone == ZONE_HOT  ? "HOT ◀" :
            s->meta[slot].zone == ZONE_WARM ? "warm"   : "cold";
        printf("  %-6d  %-8d  %-12lld  %5.2f%%  %s\n",
               slot,
               s->meta[slot].handle,
               counts[r].count,
               pct,
               zone_str);
    }
    printf("\n");
    free(counts);
}
