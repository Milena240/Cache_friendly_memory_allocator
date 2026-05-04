#ifndef ARRAY_PROFILER_H
#define ARRAY_PROFILER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    long long* counts;
    int        size;
    long long  total;
} ArrayProfiler;

void  profiler_init   (ArrayProfiler* p, int size);
void  profiler_destroy(ArrayProfiler* p);
void  profiler_reset  (ArrayProfiler* p);

static inline void profiler_record(ArrayProfiler* p, int i) {
    if (i >= 0 && i < p->size) {
        p->counts[i]++;
        p->total++;
    }
}

void print_access_report(ArrayProfiler* p, const char* title, int top_n);


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


typedef struct {
    int* data;
    int* new_to_old;
    int* old_to_new;
    int  size;
    int  n_hot;
} ReorderedArray;

ReorderedArray* reorder_hot_first(ArrayProfiler* p,
                                   int* arr,
                                   double hot_threshold);
void reordered_destroy(ReorderedArray* r);

void print_reorder_map(ReorderedArray* r, int show_n);

#endif /* ARRAY_PROFILER_H */
