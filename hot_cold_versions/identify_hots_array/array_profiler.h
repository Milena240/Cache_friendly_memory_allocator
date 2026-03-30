#ifndef ARRAY_PROFILER_H
#define ARRAY_PROFILER_H

#include <stddef.h>
#include <stdint.h>

/* =========================================================
 * array_profiler.h  —  Runtime array element access profiler
 *
 * WHAT IT DOES
 * ────────────
 * Tracks how many times each index of an array is accessed.
 * After running a workload, prints a report showing:
 *   - which indices are HOT (accessed often)
 *   - which indices are COLD (rarely accessed)
 *   - a recommendation for how to reorder the array
 *
 * Then reorders the array putting hot elements at the front
 * so they all fit in the first few cache lines.
 * Measures the speedup before and after reordering.
 *
 * HOW IT WORKS
 * ────────────
 * Every array access goes through a macro:
 *   GET_ELEM(profiler, array, index)
 *   SET_ELEM(profiler, array, index, value)
 *
 * The macro increments a counter for that index, then
 * does the actual array read/write.
 *
 * After the workload you call:
 *   print_access_report()  — see which indices are hot
 *   reorder_hot_first()    — move hot elements to front
 * ========================================================= */


/* ── ArrayProfiler ───────────────────────────────────────
 * Holds one counter per array element.
 * Dynamically sized to match the array being profiled.
 */
typedef struct {
    long long* counts;      /* access count per index        */
    int        size;        /* number of elements tracked    */
    long long  total;       /* total accesses recorded       */
} ArrayProfiler;

void  profiler_init   (ArrayProfiler* p, int size);
void  profiler_destroy(ArrayProfiler* p);
void  profiler_reset  (ArrayProfiler* p);

/* Record one access to index i */
static inline void profiler_record(ArrayProfiler* p, int i) {
    if (i >= 0 && i < p->size) {
        p->counts[i]++;
        p->total++;
    }
}

/* Print full report: which indices are hot/cold */
void print_access_report(ArrayProfiler* p, const char* title,
                         int top_n);   /* show top N hottest indices */


/* ── Access macros ───────────────────────────────────────
 * Use these instead of arr[i] to track accesses.
 * When ARRAY_PROFILING=0 they compile away to zero cost.
 */
#define ARRAY_PROFILING 1

#if ARRAY_PROFILING
  #define GET_ELEM(p, arr, i) \
      (profiler_record((p), (i)), (arr)[i])

  #define SET_ELEM(p, arr, i, val) \
      (profiler_record((p), (i)), (arr)[i] = (val))
#else
  #define GET_ELEM(p, arr, i)        ((arr)[i])
  #define SET_ELEM(p, arr, i, val)   ((arr)[i] = (val))
#endif


/* ── Reordering ──────────────────────────────────────────
 * After profiling, reorder the array so hot elements
 * come first. Returns a mapping array so you can still
 * find where each original element went.
 *
 * new_arr[0..n_hot-1]    = hot elements (most accessed first)
 * new_arr[n_hot..size-1] = cold elements
 *
 * mapping[new_index] = original_index
 */
typedef struct {
    int* data;          /* the reordered array values        */
    int* new_to_old;    /* new_to_old[i] = original index    */
    int* old_to_new;    /* old_to_new[i] = new index         */
    int  size;
    int  n_hot;         /* how many elements are hot         */
} ReorderedArray;

/* hot_threshold: fraction of total accesses above which
 * an element is considered hot. e.g. 0.01 = 1% */
ReorderedArray* reorder_hot_first(ArrayProfiler* p,
                                   int* arr,
                                   double hot_threshold);
void reordered_destroy(ReorderedArray* r);

/* Print a visual map showing old vs new positions */
void print_reorder_map(ReorderedArray* r, int show_n);

#endif /* ARRAY_PROFILER_H */
