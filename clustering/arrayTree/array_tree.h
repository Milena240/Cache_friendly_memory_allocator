#ifndef ARRAY_TREE_H
#define ARRAY_TREE_H

#include <stddef.h>
#include <stdint.h>

/* =========================================================
 * array_tree.h  —  Cache-friendly implicit binary search tree
 *
 * CORE IDEA
 * ─────────
 * A normal BST stores structure in pointers:
 *   node->left, node->right
 * Every traversal step must follow a pointer to an unknown
 * address — the CPU cannot prefetch it until the pointer
 * is loaded, causing a pipeline stall on every level.
 *
 * This tree stores structure in array indices:
 *   left  child of node at index i  →  index 2*i + 1
 *   right child of node at index i  →  index 2*i + 2
 *   parent of node at index i       →  index (i-1) / 2
 *
 * No pointers at all. The CPU calculates the next index
 * with arithmetic — no memory fetch needed to know where
 * to look next. All nodes live in one flat array.
 *
 * MEMORY LAYOUT
 * ─────────────
 * For a tree with capacity 7 (depth 2):
 *
 *   array: [v0][v1][v2][v3][v4][v5][v6]
 *
 *   tree shape:        v0
 *                    /    \
 *                  v1      v2
 *                 /  \    /  \
 *               v3   v4  v5   v6
 *
 *   When traversing: every node and its children fit in
 *   the same or adjacent cache lines — no pointer jump.
 *
 * OPERATIONS
 * ──────────
 *   insert: place value at correct BST position,
 *           shift values down to maintain BST property
 *   search: calculate index with arithmetic, no pointer follow
 *   delete: replace with sentinel, rebuild subtree
 *   traverse: linear scan of array — perfectly sequential
 *
 * COMPARISON TARGET
 * ─────────────────
 *   PointerTree  — standard BST with left/right pointers
 *                  allocated from a pool (your existing tree)
 *   ArrayTree    — this file, no pointers, flat array
 * ========================================================= */

#define ARRAY_TREE_CAPACITY  (1 << 20)   /* 1M nodes max    */
#define EMPTY_SLOT           INT32_MIN   /* sentinel value  */

/* ── Array tree ──────────────────────────────────────────
 * The entire tree is one flat array of integers.
 * Position encodes structure — no pointers needed.
 */
typedef struct {
    int32_t* data;      /* flat array of node values     */
    int      capacity;  /* max nodes (must be 2^k - 1)   */
    int      size;      /* number of values inserted      */
} ArrayTree;

void  atree_init   (ArrayTree* t, int capacity);
void  atree_destroy(ArrayTree* t);
void  atree_insert (ArrayTree* t, int value);
int   atree_search (ArrayTree* t, int value);
long  atree_traverse_sum(ArrayTree* t);         /* sum all values  */
int   atree_count  (ArrayTree* t);              /* count non-empty */
void  atree_clear  (ArrayTree* t);

/* Index arithmetic — the heart of the structure */
static inline int atree_left  (int i) { return 2*i + 1; }
static inline int atree_right (int i) { return 2*i + 2; }
static inline int atree_parent(int i) { return (i-1) / 2; }


/* ── Pointer tree (standard BST for comparison) ──────────
 * Uses a flat pool array for storage but has left/right
 * pointers — your existing tree from node_pool.h,
 * rewritten here standalone so the benchmark is self-contained.
 */
typedef struct PNode {
    int32_t       value;
    struct PNode* left;
    struct PNode* right;
} PNode;

typedef struct {
    PNode* pool;    /* flat array — nodes allocated from here */
    int    top;     /* next free slot                         */
    int    capacity;
    PNode* root;
} PointerTree;

void   ptree_init    (PointerTree* t, int capacity);
void   ptree_destroy (PointerTree* t);
void   ptree_insert  (PointerTree* t, int value);
int    ptree_search  (PointerTree* t, int value);
long   ptree_traverse_sum(PointerTree* t);
int    ptree_count   (PointerTree* t);
void   ptree_clear   (PointerTree* t);


/* ── Metrics ─────────────────────────────────────────────*/
void print_tree_layout_info(ArrayTree* at, PointerTree* pt);

#endif /* ARRAY_TREE_H */
