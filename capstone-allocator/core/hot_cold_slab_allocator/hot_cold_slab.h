#ifndef HOT_COLD_SLAB_H
#define HOT_COLD_SLAB_H

#include <stddef.h>
#include <stdint.h>

#define SLAB_MAX_SLOTS      65536   
#define SLAB_EPOCH_SIZE     10000   
#define HOT_PERCENTILE      0.20    

typedef int SlabHandle;           
#define INVALID_HANDLE  (-1)

typedef enum {
    SLOT_FREE = 0,
    SLOT_USED = 1
} SlotState;

typedef enum {
    ZONE_HOT  = 0,  
    ZONE_WARM = 1,  
    ZONE_COLD = 2  
} SlotZone;

typedef struct {
    SlotState  state;   
    SlotZone   zone;        
    SlabHandle handle;      
    long long  access_count; 
    long long  total_count;  
} SlotMeta;

typedef struct {
    char*      data;
    size_t     obj_size;
    int        capacity;
    int        n_used;

    SlotMeta*  meta;

    int*       handle_to_slot;
    int*       slot_to_handle;
    int        n_handles;

    int*       free_stack;
    int        free_top;

    long long  epoch_accesses;
    int        epoch_size;
    int        n_epochs;
    int        n_compactions;

    int        n_hot_slots;
    int        n_warm_slots;
    int        n_cold_slots;

    int        data_is_mmap;
    size_t     data_mmap_size;
} HotColdSlab;

HotColdSlab* slab_create (size_t obj_size, int capacity);
void         slab_destroy(HotColdSlab* s);

SlabHandle   slab_alloc  (HotColdSlab* s);
void         slab_free   (HotColdSlab* s, SlabHandle h);
void*        slab_get    (HotColdSlab* s, SlabHandle h);
void*        slab_get_raw(HotColdSlab* s, SlabHandle h);
void         slab_epoch_check(HotColdSlab* s);
void         slab_compact(HotColdSlab* s);


typedef struct {
    int       capacity;
    int       n_used;
    int       n_hot;
    int       n_warm;
    int       n_cold;
    int       n_epochs;
    int       n_compactions;
    size_t    obj_size;
    size_t    hot_zone_bytes;   
    int       hot_cache_lines;  
    double    hot_fraction;    
} SlabStats;

SlabStats slab_get_stats (HotColdSlab* s);
void      slab_print_stats(HotColdSlab* s, const char* label);
void      slab_print_layout(HotColdSlab* s, int show_n);
void      slab_print_access_report(HotColdSlab* s, int top_n);

#endif /* HOT_COLD_SLAB_H */
