#ifndef DYNAMIC_POOL_H
#define DYNAMIC_POOL_H

#include <stddef.h>
#include <stdint.h>

#define CHUNK_SIZE 64   /* nodes per chunk */


/* ── Linked-list node ─────────────────────────────────── */

typedef struct DListNode {
    int              value;
    struct DListNode* next;
} DListNode;

typedef struct DListChunk {
    DListNode          storage[CHUNK_SIZE];
    int                used;               /* slots filled in this chunk */
    struct DListChunk* next_chunk;         /* pointer to next chunk      */
} DListChunk;

typedef struct {
    DListChunk* head;      /* first chunk (oldest)   */
    DListChunk* current;   /* chunk being filled now */
    int         total;     /* total nodes allocated  */
    int         n_chunks;  /* number of chunks so far */
} DynamicListPool;

void       dynamic_list_init  (DynamicListPool* p);
DListNode* dynamic_list_alloc (DynamicListPool* p, int value);
void       dynamic_list_reset (DynamicListPool* p);   /* free all chunks */


/* ── Binary-tree node ─────────────────────────────────── */

typedef struct DTreeNode {
    int              value;
    struct DTreeNode* left;
    struct DTreeNode* right;
} DTreeNode;

typedef struct DTreeChunk {
    DTreeNode         storage[CHUNK_SIZE];
    int               used;
    struct DTreeChunk* next_chunk;
} DTreeChunk;

typedef struct {
    DTreeChunk* head;
    DTreeChunk* current;
    int         total;
    int         n_chunks;
} DynamicTreePool;

void       dynamic_tree_init  (DynamicTreePool* p);
DTreeNode* dynamic_tree_alloc (DynamicTreePool* p, int value);
void       dynamic_tree_reset (DynamicTreePool* p);


/* ── List builders ────────────────────────────────────── */

DListNode* dlist_build_pooled   (DynamicListPool* p, int size);

DListNode* dlist_build_scattered(int size);
void       dlist_free_scattered (DListNode* head);


/* ── Tree builders ────────────────────────────────────── */

DTreeNode* dtree_build_pooled   (DynamicTreePool* p, int size);

DTreeNode* dtree_build_scattered(int size);
void       dtree_free_scattered (DTreeNode* root);


/* ── Workloads ────────────────────────────────────────── */

long dlist_traverse_sum  (DListNode* head);
long dtree_traverse_count(DTreeNode* root);
int  dtree_search        (DTreeNode* root, int value);


/* ── Locality metrics ─────────────────────────────────── */

double dlist_avg_stride(DListNode* head);

typedef struct {
    int    n_chunks;
    int    total_nodes;
    size_t bytes_allocated;   /* total bytes across all chunks */
    size_t bytes_used;        /* bytes actually holding nodes  */
    double utilization;       /* bytes_used / bytes_allocated  */
} PoolStats;

PoolStats dynamic_list_stats(DynamicListPool* p);
PoolStats dynamic_tree_stats(DynamicTreePool* p);

void print_pool_stats(const char* label, PoolStats s);

#endif /* DYNAMIC_POOL_H */
