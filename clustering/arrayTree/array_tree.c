#include "array_tree.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* =========================================================
 * array_tree.c  —  Implementation
 * ========================================================= */


/* ── ArrayTree ───────────────────────────────────────────*/

void atree_init(ArrayTree* t, int capacity) {
    t->data     = (int32_t*)malloc(sizeof(int32_t) * (size_t)capacity);
    t->capacity = capacity;
    t->size     = 0;
    /* fill every slot with the empty sentinel */
    for (int i = 0; i < capacity; i++)
        t->data[i] = EMPTY_SLOT;
}

void atree_destroy(ArrayTree* t) {
    free(t->data);
    t->data     = NULL;
    t->capacity = 0;
    t->size     = 0;
}

void atree_clear(ArrayTree* t) {
    for (int i = 0; i < t->capacity; i++)
        t->data[i] = EMPTY_SLOT;
    t->size = 0;
}

/* insert: walk the tree using index arithmetic.
 * At each step, go left if value < current, right if value >.
 * When an empty slot is found, place the value there.
 * This maintains BST property without touching any pointer. */
void atree_insert(ArrayTree* t, int value) {
    int i = 0;   /* start at root — always index 0 */

    while (i < t->capacity) {
        if (t->data[i] == EMPTY_SLOT) {
            /* empty slot — place value here */
            t->data[i] = value;
            t->size++;
            return;
        }
        if (value < t->data[i])
            i = atree_left(i);    /* go left:  2*i + 1 */
        else if (value > t->data[i])
            i = atree_right(i);   /* go right: 2*i + 2 */
        else
            return;               /* duplicate — ignore */
    }
    /* capacity exceeded — tree is full */
}

/* search: same index arithmetic, no pointer follow.
 * The CPU can calculate 2*i+1 or 2*i+2 before the
 * current cache line is even fully loaded. */
int atree_search(ArrayTree* t, int value) {
    int i = 0;

    while (i < t->capacity) {
        if (t->data[i] == EMPTY_SLOT)
            return 0;             /* not found */
        if (value == t->data[i])
            return 1;             /* found */
        if (value < t->data[i])
            i = atree_left(i);
        else
            i = atree_right(i);
    }
    return 0;
}

/* traverse: just scan the array linearly.
 * Skip empty slots, sum all real values.
 * This is a perfectly sequential memory access — ideal for cache. */
long atree_traverse_sum(ArrayTree* t) {
    long sum = 0;
    for (int i = 0; i < t->capacity; i++)
        if (t->data[i] != EMPTY_SLOT)
            sum += t->data[i];
    return sum;
}

int atree_count(ArrayTree* t) {
    return t->size;
}


/* ── PointerTree ─────────────────────────────────────────*/

void ptree_init(PointerTree* t, int capacity) {
    t->pool     = (PNode*)malloc(sizeof(PNode) * (size_t)capacity);
    t->capacity = capacity;
    t->top      = 0;
    t->root     = NULL;
}

void ptree_destroy(PointerTree* t) {
    free(t->pool);
    t->pool     = NULL;
    t->root     = NULL;
    t->top      = 0;
}

void ptree_clear(PointerTree* t) {
    t->top  = 0;
    t->root = NULL;
}

static PNode* ptree_alloc(PointerTree* t, int value) {
    if (t->top >= t->capacity) return NULL;
    PNode* n  = &t->pool[t->top++];
    n->value  = value;
    n->left   = NULL;
    n->right  = NULL;
    return n;
}

static PNode* ptree_insert_rec(PointerTree* t, PNode* node, int value) {
    if (!node) return ptree_alloc(t, value);
    if (value < node->value)
        node->left  = ptree_insert_rec(t, node->left,  value);
    else if (value > node->value)
        node->right = ptree_insert_rec(t, node->right, value);
    return node;
}

void ptree_insert(PointerTree* t, int value) {
    t->root = ptree_insert_rec(t, t->root, value);
}

int ptree_search(PointerTree* t, int value) {
    PNode* cur = t->root;
    while (cur) {
        if      (value < cur->value) cur = cur->left;
        else if (value > cur->value) cur = cur->right;
        else                         return 1;
    }
    return 0;
}

/* in-order traversal — recursive */
static long ptree_inorder_sum(PNode* node) {
    if (!node) return 0;
    return node->value
         + ptree_inorder_sum(node->left)
         + ptree_inorder_sum(node->right);
}

long ptree_traverse_sum(PointerTree* t) {
    return ptree_inorder_sum(t->root);
}

int ptree_count(PointerTree* t) {
    return t->top;
}


/* ── Layout info ─────────────────────────────────────────*/

void print_tree_layout_info(ArrayTree* at, PointerTree* pt) {
    printf("  Memory layout comparison\n");
    printf("  ────────────────────────────────────────────────\n");
    printf("  ArrayTree:\n");
    printf("    Storage:    one flat int32_t array\n");
    printf("    Node size:  %zu bytes  (value only, no pointers)\n",
           sizeof(int32_t));
    printf("    Total size: %zu KB  for %d slots\n",
           (size_t)at->capacity * sizeof(int32_t) / 1024,
           at->capacity);
    printf("    Values per cache line: %zu\n",
           64 / sizeof(int32_t));
    printf("    Traversal:  linear scan — perfectly sequential\n");
    printf("    Navigation: index arithmetic  2*i+1 / 2*i+2\n\n");

    printf("  PointerTree (pool-allocated):\n");
    printf("    Storage:    pool array of PNode structs\n");
    printf("    Node size:  %zu bytes  (value + 2 pointers)\n",
           sizeof(PNode));
    printf("    Total size: %zu KB  for %d slots\n",
           (size_t)pt->capacity * sizeof(PNode) / 1024,
           pt->capacity);
    printf("    Values per cache line: %zu\n",
           64 / sizeof(PNode));
    printf("    Traversal:  pointer chasing — follows left/right\n");
    printf("    Navigation: pointer dereference\n\n");

    printf("  Key difference:\n");
    printf("    ArrayTree node is %zu bytes vs %zu bytes for PointerTree.\n",
           sizeof(int32_t), sizeof(PNode));
    printf("    ArrayTree fits %zu values per cache line vs %zu nodes.\n",
           64 / sizeof(int32_t),
           64 / sizeof(PNode));
}
