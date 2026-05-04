#ifndef MULTI_SLAB_C
#define MULTI_SLAB_C

#include "multi_slab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int 
mslab_class_for_size(size_t size)
{
    for (int i = 0; i < N_SIZE_CLASSES; i++)
        if (size <= SIZE_CLASS_BYTES[i])
            return i;
    return -1;  
}

int
mslab_objects_per_cache_line(int class_index)
{
    if (class_index < 0 || class_index >= N_SIZE_CLASSES) return 0;
    size_t obj_size = SIZE_CLASS_BYTES[class_index];
    if (obj_size >= 64) return 1;
    return (int)(64 / obj_size);
}

MultiSlab*
mslab_create(void) 
{
    MultiSlab* m = (MultiSlab*)calloc(1, sizeof(MultiSlab));

    for (int i = 0; i < N_SIZE_CLASSES; i++)
        m->slabs[i] = slab_create(SIZE_CLASS_BYTES[i],
                                   SIZE_CLASS_CAPACITY[i]);
    return m;
}

void
mslab_destroy(MultiSlab* m)
{
    for (int i = 0; i < N_SIZE_CLASSES; i++)
        slab_destroy(m->slabs[i]);
    free(m);
}

AllocHandle 
mslab_alloc(MultiSlab* m, size_t size)
{
    AllocHandle h;
    h.large_ptr = NULL;

    int ci = mslab_class_for_size(size);

    if (ci < 0) {
        h.slab_index  = -1;
        h.slot_handle = -1;
        h.large_ptr   = malloc(size);
        m->large_allocs++;
        m->total_allocs++;
        return h;
    }

    h.slab_index  = ci;
    h.slot_handle = slab_alloc(m->slabs[ci]);

    if (h.slot_handle == INVALID_HANDLE) {
        fprintf(stderr, "multi_slab: size class %d is full\n", ci);
        h.slab_index  = -2;  
        h.slot_handle = -1;
        return h;
    }

    m->class_allocs[ci]++;
    m->total_allocs++;
    return h;
}

void
mslab_free(MultiSlab* m, AllocHandle h)
{
    if (h.slab_index == -1) {
        free(h.large_ptr);
        m->total_frees++;
        return;
    }
    if (h.slab_index < 0 || h.slab_index >= N_SIZE_CLASSES) return;

    slab_free(m->slabs[h.slab_index], h.slot_handle);
    m->total_frees++;
}

void*
mslab_get(MultiSlab* m, AllocHandle h) 
{
    if (h.slab_index == -1) return h.large_ptr;
    if (h.slab_index < 0 || h.slab_index >= N_SIZE_CLASSES) return NULL;
    return slab_get(m->slabs[h.slab_index], h.slot_handle);
}

void*
mslab_get_raw(MultiSlab* m, AllocHandle h)
{
    if (h.slab_index == -1) return h.large_ptr;
    if (h.slab_index < 0 || h.slab_index >= N_SIZE_CLASSES) return NULL;
    return slab_get_raw(m->slabs[h.slab_index], h.slot_handle);
}

void 
mslab_compact_all(MultiSlab* m)
{
    for (int i = 0; i < N_SIZE_CLASSES; i++)
        slab_compact(m->slabs[i]);
}

void
mslab_compact_class(MultiSlab* m, int class_index)
{
    if (class_index < 0 || class_index >= N_SIZE_CLASSES) return;
    slab_compact(m->slabs[class_index]);
}

void 
mslab_print_summary(MultiSlab* m)
{
    printf("  Multi-size hot-cold allocator summary\n");
    printf("  ─────────────────────────────────────────────────────\n");
    printf("  %-10s  %-8s  %-8s  %-8s  %-8s  %-10s\n",
           "Size class", "Capacity", "Used", "Hot", "Epochs", "Obj/line");
    printf("  %-10s  %-8s  %-8s  %-8s  %-8s  %-10s\n",
           "──────────", "────────", "────", "───", "──────", "────────");

    for (int i = 0; i < N_SIZE_CLASSES; i++) {
        HotColdSlab* s  = m->slabs[i];
        SlabStats    st = slab_get_stats(s);
        printf("  %-10zu  %-8d  %-8d  %-8d  %-8d  %-10d\n",
               SIZE_CLASS_BYTES[i],
               st.capacity,
               st.n_used,
               st.n_hot,
               st.n_epochs,
               mslab_objects_per_cache_line(i));
    }

    printf("\n");
    printf("  Total allocations: %lld\n",  m->total_allocs);
    printf("  Total frees:       %lld\n",  m->total_frees);
    printf("  Large (malloc):    %lld\n\n", m->large_allocs);
}

void 
mslab_print_class_stats(MultiSlab* m, int ci)
{
    if (ci < 0 || ci >= N_SIZE_CLASSES) return;
    HotColdSlab* s = m->slabs[ci];
    printf("  Size class %zu bytes:\n", SIZE_CLASS_BYTES[ci]);
    slab_print_stats(s, "");
    slab_print_access_report(s, 10);
}

void 
mslab_print_all_classes(MultiSlab* m) 
{
    for (int i = 0; i < N_SIZE_CLASSES; i++) {
        if (m->slabs[i]->n_used > 0)
            mslab_print_class_stats(m, i);
    }
}

#endif /* MULTI_SLAB_C */

