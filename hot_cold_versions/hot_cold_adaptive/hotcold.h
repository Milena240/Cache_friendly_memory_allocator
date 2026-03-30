#ifndef HOTCOLD_H
#define HOTCOLD_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================
 * hotcold.h  —  Hot/Cold struct splitting experiment
 *
 * Core idea: a struct with many fields wastes cache space
 * if only a few fields are accessed in a tight loop.
 * The CPU loads the entire cache line (64 bytes) regardless
 * of how many fields you actually touch.
 *
 * Solution: split the struct into a "hot" part (frequently
 * accessed fields) and a "cold" part (rarely accessed fields).
 * The hot part is kept small so more objects fit per cache
 * line. The cold part is accessed via a pointer only when
 * needed.
 *
 * We test three layouts:
 *
 *   UNSPLIT  — all 10 fields in one struct (72 bytes)
 *              Only 2 fields used in the hot loop,
 *              but 72 bytes loaded per object anyway.
 *
 *   SPLIT    — hot fields in one struct (8 bytes),
 *              cold fields behind a pointer.
 *              Hot loop loads only 8 bytes per object →
 *              8x more objects per cache line.
 *
 *   PADDED   — same as UNSPLIT but fields reordered so
 *              hot fields are first. Shows that reordering
 *              alone (without splitting) helps less than
 *              a full split.
 * ========================================================= */


/* ── Layout 1: Unsplit (baseline) ───────────────────────
 *
 * All fields packed together. Total size: 72 bytes.
 * Hot loop touches only `counter` and `flags`.
 * The 8 cold fields come along for the ride every time.
 *
 * Cache lines used per object: 72/64 → 2 cache lines
 * Objects per cache line:       64/72 → less than 1
 */
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


/* ── Layout 2: Split ────────────────────────────────────
 *
 * Hot fields in their own tiny struct: 8 bytes.
 * Cold fields behind a pointer.
 *
 * Cache lines used per hot object: 8/64 → 0.125
 * Objects per cache line:          64/8  → 8
 *
 * Hot loop never touches ColdData at all.
 */
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


/* ── Layout 3: Padded (hot fields first, no split) ──────
 *
 * Same as Unsplit but hot fields guaranteed at offset 0.
 * Cold fields pushed to the back.
 * Shows that field reordering alone is not enough.
 */
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


/* ── Array builders ─────────────────────────────────────
 * Allocate and initialise arrays of N objects.
 * Caller frees with the matching free function.
 */
ObjectUnsplit* build_unsplit (int n);
void           free_unsplit  (ObjectUnsplit* arr);

/* For split: builds parallel arrays —
 *   hot[]  holds the hot structs (contiguous, cache-friendly)
 *   cold[] holds the cold structs (separate allocation)
 *   hot[i].cold points to &cold[i]
 */
ObjectSplit*   build_split_hot  (int n);
ColdData*      build_split_cold (int n, ObjectSplit* hot);
void           free_split       (ObjectSplit* hot, ColdData* cold);

ObjectPadded*  build_padded (int n);
void           free_padded  (ObjectPadded* arr);


/* ── Workloads ───────────────────────────────────────────
 *
 * Each workload takes the array and a "cold_every" parameter:
 *   cold_every = 0   → never touch cold fields  (pure hot loop)
 *   cold_every = 10  → touch cold fields every 10th object
 *   cold_every = 2   → touch cold fields every 2nd object
 *
 * The hot work: increment counter, flip a bit in flags.
 * The cold work: add weight to a running sum.
 *
 * We return a value so the compiler cannot dead-code-eliminate
 * the loops.
 */
long workload_unsplit (ObjectUnsplit* arr, int n, int cold_every);
long workload_split   (ObjectSplit*   arr, int n, int cold_every);
long workload_padded  (ObjectPadded*  arr, int n, int cold_every);

/* ── Struct size reporter ────────────────────────────── */
void print_struct_sizes(void);

#endif /* HOTCOLD_H */
