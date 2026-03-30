#ifndef NODE_POOL_C
#define NODE_POOL_C

#include "node_pool.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>


void list_pool_init(ListPool* p) {
    p->top      = 0;
    p->capacity = POOL_CAPACITY;
}

ListNode* list_pool_alloc(ListPool* p, int value) {
    if (p->top >= p->capacity) {
        fprintf(stderr, "ListPool exhausted\n");
        return NULL;
    }
    ListNode* n = &p->storage[p->top++];
    n->value = value;
    n->next  = NULL;
    return n;
}

void list_pool_reset(ListPool* p) {
    p->top = 0;
}


void tree_pool_init(TreePool* p) {
    p->top      = 0;
    p->capacity = POOL_CAPACITY;
}

TreeNode* tree_pool_alloc(TreePool* p, int value) {
    if (p->top >= p->capacity) {
        fprintf(stderr, "TreePool exhausted\n");
        return NULL;
    }
    TreeNode* n = &p->storage[p->top++];
    n->value = value;
    n->left  = NULL;
    n->right = NULL;
    return n;
}

void tree_pool_reset(TreePool* p) {
    p->top = 0;
}

ListNode* list_build_scattered(int size) {
    ListNode* head = NULL;
    ListNode* tail = NULL;
    for (int i = 0; i < size; i++) {
        ListNode* n = (ListNode*)malloc(sizeof(ListNode));
        n->value = i;
        n->next  = NULL;
        if (!head) head = n;
        else       tail->next = n;
        tail = n;
    }
    return head;
}

void list_free_scattered(ListNode* head) {
    while (head) {
        ListNode* tmp = head->next;
        free(head);
        head = tmp;
    }
}

ListNode* list_build_pooled(ListPool* pool, int size) {
    list_pool_reset(pool);
    ListNode* head = NULL;
    ListNode* tail = NULL;
    for (int i = 0; i < size; i++) {
        ListNode* n = list_pool_alloc(pool, i);
        if (!head) head = n;
        else       tail->next = n;
        tail = n;
    }
    return head;
}

static TreeNode* _build_balanced_scattered(int lo, int hi) {
    if (lo > hi) return NULL;
    int mid = lo + (hi - lo) / 2;
    TreeNode* n = (TreeNode*)malloc(sizeof(TreeNode));
    n->value = mid;
    n->left  = _build_balanced_scattered(lo, mid - 1);
    n->right = _build_balanced_scattered(mid + 1, hi);
    return n;
}

TreeNode* tree_build_scattered(int size) {
    return _build_balanced_scattered(0, size - 1);
}

static void _tree_free(TreeNode* root) {
    if (!root) return;
    _tree_free(root->left);
    _tree_free(root->right);
    free(root);
}

void tree_free_scattered(TreeNode* root) {
    _tree_free(root);
}

static TreeNode* _build_balanced_pooled(TreePool* pool, int lo, int hi) {
    if (lo > hi) return NULL;
    int mid = lo + (hi - lo) / 2;
    TreeNode* n = tree_pool_alloc(pool, mid);
    n->left  = _build_balanced_pooled(pool, lo, mid - 1);
    n->right = _build_balanced_pooled(pool, mid + 1, hi);
    return n;
}

TreeNode* tree_build_pooled(TreePool* pool, int size) {
    tree_pool_reset(pool);
    return _build_balanced_pooled(pool, 0, size - 1);
}

long list_traverse_sum(ListNode* head) {
    long sum = 0;
    while (head) {
        sum += head->value;
        head = head->next;
    }
    return sum;
}

long tree_traverse_count(TreeNode* root) {
    if (!root) return 0;
    return 1
         + tree_traverse_count(root->left)
         + tree_traverse_count(root->right);
}

int tree_search(TreeNode* root, int value) {
    while (root) {
        if      (value < root->value) root = root->left;
        else if (value > root->value) root = root->right;
        else                          return 1;
    }
    return 0;
}

double list_avg_stride(ListNode* head) {
    if (!head || !head->next) return 0.0;
    long long total = 0;
    long      count = 0;
    ListNode* cur = head;
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

double tree_avg_stride(TreeNode* root, int size) {
    if (!root || size <= 1) return 0.0;

    TreeNode** stack = (TreeNode**)malloc(sizeof(TreeNode*) * (size_t)(size + 64));
    uintptr_t* addrs = (uintptr_t*)malloc(sizeof(uintptr_t) * (size_t)size);
    int        sp    = 0;
    int        ai    = 0;
    TreeNode*  cur   = root;

    while (cur || sp > 0) {
        while (cur) { stack[sp++] = cur; cur = cur->left; }
        cur = stack[--sp];
        addrs[ai++] = (uintptr_t)cur;
        cur = cur->right;
    }

    long long total = 0;
    for (int i = 0; i < ai - 1; i++) {
        long long diff = (long long)addrs[i+1] - (long long)addrs[i];
        if (diff < 0) diff = -diff;
        total += diff;
    }

    free(stack);
    free(addrs);

    return (double)total / (double)(ai - 1);
}

#endif /* NODE_POOL_C */


