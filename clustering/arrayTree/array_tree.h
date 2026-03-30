#ifndef ARRAY_TREE_H
#define ARRAY_TREE_H

#include <stddef.h>
#include <stdint.h>
#define ARRAY_TREE_CAPACITY  (1 << 20)   /* 1M nodes max    */
#define EMPTY_SLOT           INT32_MIN   /* sentinel value  */

typedef struct {
    int32_t* data;
    int      capacity;  
    int      size;     
} ArrayTree;

void  atree_init   (ArrayTree* t, int capacity);
void  atree_destroy(ArrayTree* t);
void  atree_insert (ArrayTree* t, int value);
int   atree_search (ArrayTree* t, int value);
long  atree_traverse_sum(ArrayTree* t);       
int   atree_count  (ArrayTree* t);             
void  atree_clear  (ArrayTree* t);

static inline int atree_left  (int i) { return 2*i + 1; }
static inline int atree_right (int i) { return 2*i + 2; }
static inline int atree_parent(int i) { return (i-1) / 2; }

typedef struct PNode {
    int32_t       value;
    struct PNode* left;
    struct PNode* right;
} PNode;

typedef struct {
    PNode* pool;        
    int    top;     
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


void print_tree_layout_info(ArrayTree* at, PointerTree* pt);

#endif /* ARRAY_TREE_H */


