#ifndef DYNAMIC_POOL_C
#define DYNAMIC_POOL_C

#include "dynamic_pool.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>


static DListChunk* new_list_chunk(void) {
    DListChunk* c = (DListChunk*)malloc(sizeof(DListChunk));
    if (!c) {
        fprintf(stderr, "dynamic_pool: out of memory\n");
        return NULL;
    }
    c->used       = 0;
    c->next_chunk = NULL;
    return c;
}

static DTreeChunk* new_tree_chunk(void) {
    DTreeChunk* c = (DTreeChunk*)malloc(sizeof(DTreeChunk));
    if (!c) {
        fprintf(stderr, "dynamic_pool: out of memory\n");
        return NULL;
    }
    c->used       = 0;
    c->next_chunk = NULL;
    return c;
}

void dynamic_list_init(DynamicListPool* p) {
    DListChunk* first = new_list_chunk();
    p->head      = first;
    p->current   = first;
    p->total     = 0;
    p->n_chunks  = 1;
}

DListNode* dynamic_list_alloc(DynamicListPool* p, int value) {
    if (p->current->used >= CHUNK_SIZE) {
        DListChunk* chunk      = new_list_chunk();
        p->current->next_chunk = chunk;   /* link old → new */
        p->current             = chunk;   /* advance current */
        p->n_chunks++;
    }

    DListNode* n = &p->current->storage[p->current->used++];
    n->value = value;
    n->next  = NULL;
    p->total++;
    return n;
}

void dynamic_list_reset(DynamicListPool* p) {
    if (!p->head) return;  
    DListChunk* chunk = p->head;
    while (chunk) {
        DListChunk* next = chunk->next_chunk;
        free(chunk);
        chunk = next;
    }
    p->head     = NULL;
    p->current  = NULL;
    p->total    = 0;
    p->n_chunks = 0;
}


void dynamic_tree_init(DynamicTreePool* p) {
    DTreeChunk* first = new_tree_chunk();
    p->head     = first;
    p->current  = first;
    p->total    = 0;
    p->n_chunks = 1;
}

DTreeNode* dynamic_tree_alloc(DynamicTreePool* p, int value) {
    if (p->current->used >= CHUNK_SIZE) {
        DTreeChunk* chunk      = new_tree_chunk();
        p->current->next_chunk = chunk;
        p->current             = chunk;
        p->n_chunks++;
    }
    DTreeNode* n = &p->current->storage[p->current->used++];
    n->value = value;
    n->left  = NULL;
    n->right = NULL;
    p->total++;
    return n;
}

void dynamic_tree_reset(DynamicTreePool* p) {
    if (!p->head) return;
    DTreeChunk* chunk = p->head;
    while (chunk) {
        DTreeChunk* next = chunk->next_chunk;
        free(chunk);
        chunk = next;
    }
    p->head     = NULL;
    p->current  = NULL;
    p->total    = 0;
    p->n_chunks = 0;
}

DListNode* dlist_build_pooled(DynamicListPool* p, int size) {
    p->head = NULL;  
    dynamic_list_reset(p);
    dynamic_list_init(p);

    DListNode* head = NULL;
    DListNode* tail = NULL;
    for (int i = 0; i < size; i++) {
        DListNode* n = dynamic_list_alloc(p, i);
        if (!head) head = n;
        else       tail->next = n;
        tail = n;
    }
    return head;
}

DListNode* dlist_build_scattered(int size) {
    DListNode* head = NULL;
    DListNode* tail = NULL;
    for (int i = 0; i < size; i++) {
        DListNode* n = (DListNode*)malloc(sizeof(DListNode));
        n->value = i;
        n->next  = NULL;
        if (!head) head = n;
        else       tail->next = n;
        tail = n;
    }
    return head;
}

void dlist_free_scattered(DListNode* head) {
    while (head) {
        DListNode* tmp = head->next;
        free(head);
        head = tmp;
    }
}


typedef struct { int lo; int hi; DTreeNode** slot; } BTask;

DTreeNode* dtree_build_pooled(DynamicTreePool* p, int size) {
    p->head = NULL;
    dynamic_tree_reset(p);
    dynamic_tree_init(p);
    if (size <= 0) return NULL;

    DTreeNode* root = NULL;
    BTask* stack = (BTask*)malloc(sizeof(BTask) * (size_t)(size + 64));
    int sp = 0;
    stack[sp++] = (BTask){ 0, size - 1, &root };

    while (sp > 0) {
        BTask t = stack[--sp];
        if (t.lo > t.hi) { *t.slot = NULL; continue; }
        int mid    = t.lo + (t.hi - t.lo) / 2;
        DTreeNode* n = dynamic_tree_alloc(p, mid);
        *t.slot    = n;
        stack[sp++] = (BTask){ t.lo,   mid - 1, &n->left  };
        stack[sp++] = (BTask){ mid + 1, t.hi,   &n->right };
    }
    free(stack);
    return root;
}

DTreeNode* dtree_build_scattered(int size) {
    if (size <= 0) return NULL;
    DTreeNode* root = NULL;
    BTask* stack = (BTask*)malloc(sizeof(BTask) * (size_t)(size + 64));
    int sp = 0;
    stack[sp++] = (BTask){ 0, size - 1, &root };

    while (sp > 0) {
        BTask t = stack[--sp];
        if (t.lo > t.hi) { *t.slot = NULL; continue; }
        int mid    = t.lo + (t.hi - t.lo) / 2;
        DTreeNode* n = (DTreeNode*)malloc(sizeof(DTreeNode));
        n->value = mid; n->left = NULL; n->right = NULL;
        *t.slot    = n;
        stack[sp++] = (BTask){ t.lo,   mid - 1, &n->left  };
        stack[sp++] = (BTask){ mid + 1, t.hi,   &n->right };
    }
    free(stack);
    return root;
}

static void _dtree_free(DTreeNode* root) {
    if (!root) return;
    _dtree_free(root->left);
    _dtree_free(root->right);
    free(root);
}

void dtree_free_scattered(DTreeNode* root) {
    _dtree_free(root);
}


/* ── Workloads ────────────────────────────────────────── */

long dlist_traverse_sum(DListNode* head) {
    long sum = 0;
    while (head) {
        sum += head->value;
        head = head->next;
    }
    return sum;
}

long dtree_traverse_count(DTreeNode* root) {
    if (!root) return 0;
    return 1
        + dtree_traverse_count(root->left)
        + dtree_traverse_count(root->right);
}

int dtree_search(DTreeNode* root, int value) {
    while (root) {
        if      (value < root->value) root = root->left;
        else if (value > root->value) root = root->right;
        else                          return 1;
    }
    return 0;
}


/* ── Locality metrics ─────────────────────────────────── */

double dlist_avg_stride(DListNode* head) {
    if (!head || !head->next) return 0.0;
    long long total = 0;
    long      count = 0;
    DListNode* cur = head;
    while (cur && cur->next) {
        long long diff = (long long)(uintptr_t)cur->next
                       - (long long)(uintptr_t)cur;
        if (diff < 0) diff = -diff;
        total += diff;
        count++;
        cur = cur->next;
    }
    return (double)total / (double)count;
}


/* ── Pool stats ───────────────────────────────────────── */

PoolStats dynamic_list_stats(DynamicListPool* p) {
    PoolStats s;
    s.n_chunks        = p->n_chunks;
    s.total_nodes     = p->total;
    s.bytes_allocated = (size_t)p->n_chunks * sizeof(DListChunk);
    s.bytes_used      = (size_t)p->total    * sizeof(DListNode);
    s.utilization     = s.bytes_allocated > 0
                      ? (double)s.bytes_used / (double)s.bytes_allocated
                      : 0.0;
    return s;
}

PoolStats dynamic_tree_stats(DynamicTreePool* p) {
    PoolStats s;
    s.n_chunks        = p->n_chunks;
    s.total_nodes     = p->total;
    s.bytes_allocated = (size_t)p->n_chunks * sizeof(DTreeChunk);
    s.bytes_used      = (size_t)p->total    * sizeof(DTreeNode);
    s.utilization     = s.bytes_allocated > 0
                      ? (double)s.bytes_used / (double)s.bytes_allocated
                      : 0.0;
    return s;
}

void print_pool_stats(const char* label, PoolStats s) {
    printf("  %-28s  chunks: %4d  nodes: %7d  "
           "alloc: %5zu KB  used: %5zu KB  util: %.1f%%\n",
           label,
           s.n_chunks,
           s.total_nodes,
           s.bytes_allocated / 1024,
           s.bytes_used      / 1024,
           s.utilization * 100.0);
}

#endif /* DYNAMIC_POOL_C */

