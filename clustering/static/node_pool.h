#ifndef NODE_POOL_H
#define NODE_POOL_H

#include <stddef.h>
#include <stdint.h>

#define POOL_CAPACITY 1 << 20   /* 1 048 576 nodes max */


/* ── List node ──────────────────────────────────────── */
typedef struct ListNode {
    int              value;
    struct ListNode* next;
} ListNode;

typedef struct {
    ListNode storage[POOL_CAPACITY];
    int      top;          
    int      capacity;
} ListPool;

void      list_pool_init  (ListPool* p);
ListNode* list_pool_alloc (ListPool* p, int value);
void      list_pool_reset (ListPool* p);  


/* ── Binary-tree node ─────────────────────────────────── */
typedef struct TreeNode {
    int              value;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;

typedef struct {
    TreeNode storage[POOL_CAPACITY];
    int      top;
    int      capacity;
} TreePool;

void      tree_pool_init  (TreePool* p);
TreeNode* tree_pool_alloc (TreePool* p, int value);
void      tree_pool_reset (TreePool* p);


/* ── Scattered (malloc-based) builders — for comparison ─ */

ListNode* list_build_scattered (int size);
void      list_free_scattered  (ListNode* head);

ListNode* list_build_pooled    (ListPool* pool, int size);

TreeNode* tree_build_scattered (int size);
void      tree_free_scattered  (TreeNode* root);

TreeNode* tree_build_pooled    (TreePool* pool, int size);


/* ── Workloads (identical logic, different memory layout) ─ */

long list_traverse_sum  (ListNode* head);

long tree_traverse_count(TreeNode* root);

int  tree_search        (TreeNode* root, int value);


/* ── Utility ─────────────────────────────────────────── */

double list_avg_stride(ListNode* head);
double tree_avg_stride(TreeNode* root, int size);

#endif /* NODE_POOL_H */
