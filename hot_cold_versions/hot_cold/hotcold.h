#ifndef HOTCOLD_H
#define HOTCOLD_H

#include <stdint.h>
#include <stddef.h>

/* ── Layout 1: Unsplit (baseline) ───────────────────────*/

typedef struct {
    /* --- hot fields (touched every iteration) --- */
    int32_t  counter;          /*  4 bytes */
    int32_t  flags;            /*  4 bytes */
    /* --- cold fields (rarely touched) --- */
    float    weight;           /*  4 bytes */
    float    score;            /*  4 bytes */
    int32_t  category;         /*  4 bytes */
    int32_t  version;          /*  4 bytes */
    int64_t  timestamp;        /*  8 bytes */
    int64_t  checksum;         /*  8 bytes */
    double   ratio;            /*  8 bytes */
    double   threshold;        /*  8 bytes */
                               /* total: 56 bytes + 8 hot = 64 bytes... */
} ObjectUnsplit;               /* actual sizeof measured at runtime     */


/* ── Layout 2: Split ────────────────────────────────────*/

typedef struct {
    float    weight;
    float    score;
    int32_t  category;
    int32_t  version;
    int64_t  timestamp;
    int64_t  checksum;
    double   ratio;
    double   threshold;
} ColdData;

typedef struct {
    /* hot fields only */
    int32_t  counter;          /*  4 bytes */
    int32_t  flags;            /*  4 bytes */
    /* pointer to cold data — only followed when needed */
    ColdData* cold;            /*  8 bytes */
} ObjectSplit;                 /* total: 16 bytes */


/* ── Layout 3: Padded (hot fields first, no split) ──────*/

typedef struct {
    /* hot fields first */
    int32_t  counter;
    int32_t  flags;
    /* cold fields follow */
    float    weight;
    float    score;
    int32_t  category;
    int32_t  version;
    int64_t  timestamp;
    int64_t  checksum;
    double   ratio;
    double   threshold;
} ObjectPadded;


/* ── Array builders ─────────────────────────────────────*/

ObjectUnsplit* build_unsplit (int n);
void           free_unsplit  (ObjectUnsplit* arr);

ObjectSplit*   build_split_hot  (int n);
ColdData*      build_split_cold (int n, ObjectSplit* hot);
void           free_split       (ObjectSplit* hot, ColdData* cold);

ObjectPadded*  build_padded (int n);
void           free_padded  (ObjectPadded* arr);


/* ── Workloads ───────────────────────────────────────────*/

long workload_unsplit (ObjectUnsplit* arr, int n, int cold_every);
long workload_split   (ObjectSplit*   arr, int n, int cold_every);
long workload_padded  (ObjectPadded*  arr, int n, int cold_every);

/* ── Struct size reporter ────────────────────────────── */
void print_struct_sizes(void);

#endif /* HOTCOLD_H */
