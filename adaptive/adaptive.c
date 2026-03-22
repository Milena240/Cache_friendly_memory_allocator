#include "adaptive.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================
 * adaptive.c  —  Implementation
 * ========================================================= */

/* Global tracker for Approach 1 */
FieldTracker g_tracker;


/* ── FieldTracker ─────────────────────────────────────── */

void tracker_init(FieldTracker* t) {
    memset(t, 0, sizeof(FieldTracker));
}

void tracker_reset(FieldTracker* t) {
    memset(t->counts, 0, sizeof(t->counts));
    t->total_accesses  = 0;
    t->epoch_accesses  = 0;
}

void print_field_report(FieldTracker* t, const char* title) {
    printf("\n  ── Field access report: %s ──\n", title);
    printf("  Total accesses recorded: %lld\n\n", t->total_accesses);

    if (t->total_accesses == 0) {
        printf("  (no accesses recorded yet)\n");
        return;
    }

    /* find max count for bar chart scaling */
    long long max_count = 1;
    for (int i = 0; i < FIELD_COUNT; i++)
        if (t->counts[i] > max_count) max_count = t->counts[i];

    printf("  %-12s %12s %8s  %s\n",
           "Field", "Accesses", "% total", "Classification");
    printf("  %-12s %12s %8s  %s\n",
           "─────", "────────", "───────", "──────────────");

    for (int i = 0; i < FIELD_COUNT; i++) {
        double pct = 100.0 * (double)t->counts[i]
                           / (double)t->total_accesses;

        /* classify: hot if > 5% of all accesses */
        const char* label = (pct > 5.0) ? "HOT  ◀" : "cold";

        /* mini bar: one block per 5% */
        char bar[24] = {0};
        int blocks = (int)(pct / 5.0);
        if (blocks > 20) blocks = 20;
        for (int b = 0; b < blocks; b++) bar[b] = '#';

        printf("  %-12s %12lld %7.2f%%  %-6s  %s\n",
               FIELD_NAMES[i],
               t->counts[i],
               pct,
               label,
               bar);
    }
    printf("\n  Recommendation: move HOT fields to a separate packed struct.\n"
           "  Cold fields can stay together behind a pointer.\n\n");
}


/* ── AdaptiveManager internals ───────────────────────────
 *
 * epoch_record: records one field access to the epoch counters
 * and triggers an epoch check when EPOCH_SIZE is reached.
 */
static inline void epoch_record(AdaptiveManager* m, FieldID fid) {
    m->epoch_counts[fid]++;
    m->epoch_total++;
    if (m->epoch_total >= EPOCH_SIZE)
        adaptive_epoch_check(m);
}

/* decide_hot_fields: looks at epoch counters, marks fields hot
 * if they exceed HOT_THRESHOLD fraction of total epoch accesses */
static void decide_hot_fields(AdaptiveManager* m) {
    m->n_hot_fields = 0;
    for (int i = 0; i < FIELD_COUNT; i++) {
        double frac = (m->epoch_total > 0)
            ? (double)m->epoch_counts[i] / (double)m->epoch_total
            : 0.0;
        m->is_hot[i] = (frac > HOT_THRESHOLD) ? 1 : 0;
        if (m->is_hot[i]) m->n_hot_fields++;
    }
}

/* promote_all: for each unsplit object, copy its hot fields
 * into the hot zone. After this, hot field reads go to hot zone. */
static void promote_all(AdaptiveManager* m) {
    for (int h = 0; h < m->n_objects; h++) {
        HandleEntry* e = &m->handles[h];
        if (e->state == STATE_UNSPLIT && m->hot_used < m->n_objects) {
            HotZoneObj* hot = &m->hot_pool[m->hot_used++];
            /* copy current values from cold zone into hot zone */
            hot->counter = e->cold->counter;
            hot->flags   = e->cold->flags;
            e->hot       = hot;
            e->state     = STATE_PROMOTED;
            m->n_promoted++;
        }
    }
}


/* ── AdaptiveManager public API ──────────────────────────*/

void adaptive_init(AdaptiveManager* m, int n_objects) {
    memset(m, 0, sizeof(AdaptiveManager));
    m->n_objects = 0;

    m->cold_pool = (ColdZoneObj*)malloc(
                       sizeof(ColdZoneObj) * (size_t)n_objects);
    m->hot_pool  = (HotZoneObj*) malloc(
                       sizeof(HotZoneObj)  * (size_t)n_objects);

    /* initially everything is unsplit — is_hot all zero */
    memset(m->is_hot, 0, sizeof(m->is_hot));
}

void adaptive_destroy(AdaptiveManager* m) {
    free(m->cold_pool);
    free(m->hot_pool);
    memset(m, 0, sizeof(AdaptiveManager));
}

ObjHandle adaptive_alloc(AdaptiveManager* m,
                          int32_t counter, int32_t flags,
                          float weight,    float score,
                          int32_t category,int32_t version,
                          int64_t timestamp,int64_t checksum,
                          double ratio,    double threshold) {
    int h = m->n_objects++;
    ColdZoneObj* c = &m->cold_pool[h];

    /* all fields go into cold zone initially */
    c->counter   = counter;
    c->flags     = flags;
    c->weight    = weight;
    c->score     = score;
    c->category  = category;
    c->version   = version;
    c->timestamp = timestamp;
    c->checksum  = checksum;
    c->ratio     = ratio;
    c->threshold = threshold;

    m->handles[h].state = STATE_UNSPLIT;
    m->handles[h].cold  = c;
    m->handles[h].hot   = NULL;

    return (ObjHandle)h;
}

/* epoch_check: called automatically every EPOCH_SIZE accesses.
 * Looks at what was accessed this epoch, decides what is hot,
 * promotes objects if needed, resets epoch counters. */
void adaptive_epoch_check(AdaptiveManager* m) {
    m->n_epochs++;

    /* step 1: decide which fields are hot based on this epoch */
    decide_hot_fields(m);

    /* step 2: if any hot fields were found, promote all objects */
    if (m->n_hot_fields > 0 && m->n_promoted < m->n_objects)
        promote_all(m);

    /* step 3: reset epoch counters for the next epoch */
    memset(m->epoch_counts, 0, sizeof(m->epoch_counts));
    m->epoch_total = 0;
}

/* ── Field accessors ──────────────────────────────────────
 *
 * Each accessor:
 *   1. Records the access to epoch counters
 *   2. If the object is promoted AND this field is hot,
 *      reads/writes from the hot zone (cache-friendly)
 *   3. Otherwise reads/writes from the cold zone
 */

int32_t adaptive_get_counter(AdaptiveManager* m, ObjHandle h) {
    epoch_record(m, FIELD_COUNTER);
    HandleEntry* e = &m->handles[h];
    if (e->state == STATE_PROMOTED)
        return e->hot->counter;
    return e->cold->counter;
}

void adaptive_set_counter(AdaptiveManager* m, ObjHandle h, int32_t v) {
    epoch_record(m, FIELD_COUNTER);
    HandleEntry* e = &m->handles[h];
    if (e->state == STATE_PROMOTED)
        e->hot->counter = v;
    else
        e->cold->counter = v;
}

int32_t adaptive_get_flags(AdaptiveManager* m, ObjHandle h) {
    epoch_record(m, FIELD_FLAGS);
    HandleEntry* e = &m->handles[h];
    if (e->state == STATE_PROMOTED)
        return e->hot->flags;
    return e->cold->flags;
}

void adaptive_set_flags(AdaptiveManager* m, ObjHandle h, int32_t v) {
    epoch_record(m, FIELD_FLAGS);
    HandleEntry* e = &m->handles[h];
    if (e->state == STATE_PROMOTED)
        e->hot->flags = v;
    else
        e->cold->flags = v;
}

float adaptive_get_weight(AdaptiveManager* m, ObjHandle h) {
    epoch_record(m, FIELD_WEIGHT);
    return m->handles[h].cold->weight;   /* always from cold zone */
}

double adaptive_get_ratio(AdaptiveManager* m, ObjHandle h) {
    epoch_record(m, FIELD_RATIO);
    return m->handles[h].cold->ratio;
}

void adaptive_print_state(AdaptiveManager* m) {
    printf("  Adaptive manager state\n");
    printf("  ───────────────────────────────────────────\n");
    printf("  Objects:         %d\n", m->n_objects);
    printf("  Epochs run:      %d\n", m->n_epochs);
    printf("  Promoted:        %d / %d\n", m->n_promoted, m->n_objects);
    printf("  Hot fields (%d):", m->n_hot_fields);
    for (int i = 0; i < FIELD_COUNT; i++)
        if (m->is_hot[i]) printf("  %s", FIELD_NAMES[i]);
    printf("\n");
    printf("  Cold fields:");
    for (int i = 0; i < FIELD_COUNT; i++)
        if (!m->is_hot[i]) printf("  %s", FIELD_NAMES[i]);
    printf("\n\n");

    /* memory layout */
    size_t hot_bytes  = (size_t)m->n_promoted * sizeof(HotZoneObj);
    size_t cold_bytes = (size_t)m->n_objects  * sizeof(ColdZoneObj);
    printf("  Hot zone:   %zu KB  (%zu B per object × %d promoted)\n",
           hot_bytes  / 1024, sizeof(HotZoneObj),  m->n_promoted);
    printf("  Cold zone:  %zu KB  (%zu B per object × %d objects)\n",
           cold_bytes / 1024, sizeof(ColdZoneObj), m->n_objects);
    printf("  Hot loop touches only hot zone: %zu KB\n\n",
           hot_bytes / 1024);
}

long adaptive_hot_workload(AdaptiveManager* m, int n, int cold_every) {
    long sum = 0;

    /* If objects are promoted, iterate the hot zone array directly —
     * this gives the same cache-line efficiency as the manual split.
     * If not yet promoted, fall back to handle-based access. */
    if (m->n_promoted == m->n_objects && m->n_objects > 0) {
        /* FAST PATH: direct iteration over packed hot zone */
        for (int i = 0; i < n; i++) {
            int idx = i % m->n_objects;
            m->hot_pool[idx].counter++;
            m->hot_pool[idx].flags ^= 0x01;
            if (cold_every > 0 && i % cold_every == 0)
                sum += (long)m->cold_pool[idx].weight;
        }
    } else {
        /* SLOW PATH: handle-based access (before promotion) */
        for (int i = 0; i < n; i++) {
            ObjHandle h = i % m->n_objects;
            adaptive_set_counter(m, h,
                adaptive_get_counter(m, h) + 1);
            adaptive_set_flags(m, h,
                adaptive_get_flags(m, h) ^ 0x01);
            if (cold_every > 0 && i % cold_every == 0)
                sum += (long)adaptive_get_weight(m, h);
        }
    }
    return sum;
}
