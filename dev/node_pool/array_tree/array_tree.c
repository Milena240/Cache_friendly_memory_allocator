#ifndef ARRAY_TREE_C
#define ARRAY_TREE_C

#include "array_tree.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

void
atree_init(ArrayTree* t, int capacity)
{
    t->data     = (int32_t*)malloc(sizeof(int32_t) * (size_t)capacity);
    t->capacity = capacity;
    t->size     = 0;
    for (int i = 0; i < capacity; i++)
        t->data[i] = EMPTY_SLOT;
}

void
atree_destroy(ArrayTree* t)
{
    free(t->data);
    t->data     = NULL;
    t->capacity = 0;
    t->size     = 0;
}

void 
atree_clear(ArrayTree* t)
{
    for (int i = 0; i < t->capacity; i++)
        t->data[i] = EMPTY_SLOT;
    t->size = 0;
}

void
atree_insert(ArrayTree* t, int value)
{
    int i = 0;  

    while (i < t->capacity) {
        if (t->data[i] == EMPTY_SLOT) {
            t->data[i] = value;
            t->size++;
            return;
        }
        if (value < t->data[i])
            i = atree_left(i);   
        else if (value > t->data[i])
            i = atree_right(i);  
        else
            return;              
    }
}

int 
atree_search(ArrayTree* t, int value)
{
    int i = 0;

    while (i < t->capacity) {
        if (t->data[i] == EMPTY_SLOT)
            return 0;             
        if (value == t->data[i])
            return 1;            
        if (value < t->data[i])
            i = atree_left(i);
        else
            i = atree_right(i);
    }
    return 0;
}

long
atree_traverse_sum(ArrayTree* t)
{
    long sum = 0;
    for (int i = 0; i < t->capacity; i++)
        if (t->data[i] != EMPTY_SLOT)
            sum += t->data[i];
    return sum;
}

int
atree_count(ArrayTree* t)
{
    return t->size;
}

void
ptree_init(PointerTree* t, int capacity)
{
    t->pool     = (PNode*)malloc(sizeof(PNode) * (size_t)capacity);
    t->capacity = capacity;
    t->top      = 0;
    t->root     = NULL;
}

void
ptree_destroy(PointerTree* t)
{
    free(t->pool);
    t->pool     = NULL;
    t->root     = NULL;
    t->top      = 0;
}

void
ptree_clear(PointerTree* t)
{
    t->top  = 0;
    t->root = NULL;
}

static PNode*
ptree_alloc(PointerTree* t, int value)
{
    if (t->top >= t->capacity) return NULL;
    PNode* n  = &t->pool[t->top++];
    n->value  = value;
    n->left   = NULL;
    n->right  = NULL;
    return n;
}

static PNode*
ptree_insert_rec(PointerTree* t, PNode* node, int value)
{
    if (!node) return ptree_alloc(t, value);
    if (value < node->value)
        node->left  = ptree_insert_rec(t, node->left,  value);
    else if (value > node->value)
        node->right = ptree_insert_rec(t, node->right, value);
    return node;
}

void
ptree_insert(PointerTree* t, int value)
{
    t->root = ptree_insert_rec(t, t->root, value);
}

int 
ptree_search(PointerTree* t, int value)
{
    PNode* cur = t->root;
    while (cur) {
        if      (value < cur->value) cur = cur->left;
        else if (value > cur->value) cur = cur->right;
        else                         return 1;
    }
    return 0;
}

static long
ptree_inorder_sum(PNode* node)
{
    if (!node) return 0;
    return node->value
         + ptree_inorder_sum(node->left)
         + ptree_inorder_sum(node->right);
}

long
ptree_traverse_sum(PointerTree* t) 
{
    return ptree_inorder_sum(t->root);
}

int
ptree_count(PointerTree* t)
{
    return t->top;
}


void 
print_tree_layout_info(ArrayTree* at, PointerTree* pt)
{
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

#endif /*ARRAY_TREE_C*/

