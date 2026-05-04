#include "array_profiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void profiler_init(ArrayProfiler* p, int size) {
    p->counts = (long long*)calloc((size_t)size, sizeof(long long));
    p->size   = size;
    p->total  = 0;
}

void profiler_destroy(ArrayProfiler* p) {
    free(p->counts);
    p->counts = NULL;
    p->size   = 0;
    p->total  = 0;
}

void profiler_reset(ArrayProfiler* p) {
    memset(p->counts, 0, sizeof(long long) * (size_t)p->size);
    p->total = 0;
}


typedef struct {
    int       index;
    long long count;
} IndexCount;

static int cmp_desc(const void* a, const void* b) {
    const IndexCount* ia = (const IndexCount*)a;
    const IndexCount* ib = (const IndexCount*)b;
    if (ib->count > ia->count) return  1;
    if (ib->count < ia->count) return -1;
    return 0;
}

void print_access_report(ArrayProfiler* p,
                          const char* title,
                          int top_n) {
    printf("\n  ── Array access report: %s ──\n", title);
    printf("  Array size:      %d elements\n", p->size);
    printf("  Total accesses:  %lld\n", p->total);

    if (p->total == 0) {
        printf("  (no accesses recorded)\n\n");
        return;
    }

    int n_accessed = 0;
    for (int i = 0; i < p->size; i++)
        if (p->counts[i] > 0) n_accessed++;

    printf("  Elements accessed: %d / %d  (%.1f%%)\n\n",
           n_accessed, p->size,
           100.0 * n_accessed / p->size);

    IndexCount* sorted = (IndexCount*)malloc(
                             sizeof(IndexCount) * (size_t)p->size);
    for (int i = 0; i < p->size; i++) {
        sorted[i].index = i;
        sorted[i].count = p->counts[i];
    }
    qsort(sorted, (size_t)p->size, sizeof(IndexCount), cmp_desc);

    long long max_count = sorted[0].count > 0 ? sorted[0].count : 1;

    double hot_threshold = 0.01;
    long long hot_min = (long long)(p->total * hot_threshold);

    if (top_n > p->size) top_n = p->size;

    printf("  %-8s  %12s  %8s  %-6s  %s\n",
           "Index", "Accesses", "%% total", "Label", "Bar");
    printf("  %-8s  %12s  %8s  %-6s  %s\n",
           "─────", "────────", "───────", "─────", "───");

    int n_hot = 0;
    for (int r = 0; r < top_n; r++) {
        if (sorted[r].count == 0) break;

        double pct  = 100.0 * (double)sorted[r].count
                            / (double)p->total;
        int    bars = (int)(20.0 * sorted[r].count / max_count);
        char   bar[24] = {0};
        for (int b = 0; b < bars; b++) bar[b] = '#';

        const char* label = (sorted[r].count >= hot_min)
                            ? "HOT ◀" : "cold";
        if (sorted[r].count >= hot_min) n_hot++;

        printf("  %-8d  %12lld  %7.2f%%  %-6s  %s\n",
               sorted[r].index,
               sorted[r].count,
               pct,
               label,
               bar);
    }

    if (top_n < p->size) {
        int remaining_hot = 0;
        for (int r = top_n; r < p->size; r++)
            if (sorted[r].count >= hot_min) remaining_hot++;
        if (remaining_hot > 0)
            printf("  ... (%d more hot elements not shown)\n",
                   remaining_hot);
        printf("  ... (%d elements with 0 accesses not shown)\n",
               p->size - n_accessed);
    }

    printf("\n  HOT elements (>= %.0f%% of accesses): %d\n",
           hot_threshold * 100, n_hot);
    printf("  COLD elements: %d\n", p->size - n_hot);
    printf("\n  Recommendation: move HOT elements to the front of\n"
           "  the array so they all fit in the first few cache lines.\n\n");

    free(sorted);
}


ReorderedArray* reorder_hot_first(ArrayProfiler* p,
                                   int* arr,
                                   double hot_threshold) {
    ReorderedArray* r = (ReorderedArray*)malloc(sizeof(ReorderedArray));
    r->size       = p->size;
    r->data       = (int*)malloc(sizeof(int) * (size_t)p->size);
    r->new_to_old = (int*)malloc(sizeof(int) * (size_t)p->size);
    r->old_to_new = (int*)malloc(sizeof(int) * (size_t)p->size);

    IndexCount* sorted = (IndexCount*)malloc(
                             sizeof(IndexCount) * (size_t)p->size);
    for (int i = 0; i < p->size; i++) {
        sorted[i].index = i;
        sorted[i].count = p->counts[i];
    }
    qsort(sorted, (size_t)p->size, sizeof(IndexCount), cmp_desc);

    long long hot_min = (long long)(p->total * hot_threshold);
    r->n_hot = 0;
    for (int i = 0; i < p->size; i++)
        if (sorted[i].count >= hot_min) r->n_hot++;

    for (int new_idx = 0; new_idx < p->size; new_idx++) {
        int old_idx            = sorted[new_idx].index;
        r->data[new_idx]       = arr[old_idx];
        r->new_to_old[new_idx] = old_idx;
        r->old_to_new[old_idx] = new_idx;
    }

    free(sorted);
    return r;
}

void reordered_destroy(ReorderedArray* r) {
    free(r->data);
    free(r->new_to_old);
    free(r->old_to_new);
    free(r);
}

void print_reorder_map(ReorderedArray* r, int show_n) {
    printf("  Reorder map (first %d elements):\n\n", show_n);
    printf("  %-12s  %-12s  %-10s\n",
           "New index", "Old index", "In cache line");
    printf("  %-12s  %-12s  %-10s\n",
           "─────────", "─────────", "─────────────");

    if (show_n > r->size) show_n = r->size;
    for (int i = 0; i < show_n; i++) {
        int cache_line = (i * (int)sizeof(int)) / 64;
        printf("  %-12d  %-12d  line %d%s\n",
               i,
               r->new_to_old[i],
               cache_line,
               i < r->n_hot ? "  ← HOT" : "");
    }
    printf("\n  Hot elements occupy cache lines 0..%d\n",
           (r->n_hot * (int)sizeof(int) - 1) / 64);
    printf("  Cold elements start at cache line %d\n\n",
           (r->n_hot * (int)sizeof(int)) / 64);
}
