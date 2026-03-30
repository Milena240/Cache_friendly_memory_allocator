#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include <stdint.h>
#include <stddef.h>

/* ── Field IDs ───────────────────────────────────────────*/
typedef enum {
    FIELD_COUNTER   = 0,
    FIELD_FLAGS     = 1,
    FIELD_WEIGHT    = 2,
    FIELD_SCORE     = 3,
    FIELD_CATEGORY  = 4,
    FIELD_VERSION   = 5,
    FIELD_TIMESTAMP = 6,
    FIELD_CHECKSUM  = 7,
    FIELD_RATIO     = 8,
    FIELD_THRESHOLD = 9,
    FIELD_COUNT     = 10  
} FieldID;

static const char* FIELD_NAMES[FIELD_COUNT] = {
    "counter", "flags", "weight", "score",
    "category", "version", "timestamp",
    "checksum", "ratio", "threshold"
};


/* ── FieldTracker ─────────────────────────────────────────*/
typedef struct {
    long long counts[FIELD_COUNT];   /* access count per field    */
    long long total_accesses;        /* sum of all field accesses */
    long long epoch_accesses;        /* accesses since last epoch */
} FieldTracker;

/* Global tracker — one instance for the whole program */
extern FieldTracker g_tracker;

void tracker_init  (FieldTracker* t);
void tracker_reset (FieldTracker* t);

/* Record one access to a field */
static inline void tracker_record(FieldTracker* t, FieldID fid) {
    t->counts[fid]++;
    t->total_accesses++;
    t->epoch_accesses++;
}

/* Print full report: access counts, percentages, hot/cold label */
void print_field_report(FieldTracker* t, const char* title);


/* ── APPROACH 1 — Instrumented struct ────────────────────*/
#define PROFILING 1   /* set to 0 to disable instrumentation */

typedef struct {
    int32_t  counter;
    int32_t  flags;
    float    weight;
    float    score;
    int32_t  category;
    int32_t  version;
    int64_t  timestamp;
    int64_t  checksum;
    double   ratio;
    double   threshold;
} InstrumentedObj;

/* Access macros — these are the key mechanism.
 * GET_FIELD(obj, field_name, field_id) records the access
 * then returns the field value.
 * SET_FIELD(obj, field_name, field_id, value) records then sets.
 */
#if PROFILING
  #define GET_FIELD(obj, fname, fid) \
      (tracker_record(&g_tracker, fid), (obj)->fname)

  #define SET_FIELD(obj, fname, fid, val) \
      (tracker_record(&g_tracker, fid), (obj)->fname = (val))
#else
  /* zero overhead — macros compile away completely */
  #define GET_FIELD(obj, fname, fid)        ((obj)->fname)
  #define SET_FIELD(obj, fname, fid, val)   ((obj)->fname = (val))
#endif

/* Convenience wrappers for each field */
#define GET_COUNTER(o)      GET_FIELD(o, counter,   FIELD_COUNTER)
#define SET_COUNTER(o, v)   SET_FIELD(o, counter,   FIELD_COUNTER, v)
#define GET_FLAGS(o)        GET_FIELD(o, flags,      FIELD_FLAGS)
#define SET_FLAGS(o, v)     SET_FIELD(o, flags,      FIELD_FLAGS, v)
#define GET_WEIGHT(o)       GET_FIELD(o, weight,     FIELD_WEIGHT)
#define SET_WEIGHT(o, v)    SET_FIELD(o, weight,     FIELD_WEIGHT, v)
#define GET_SCORE(o)        GET_FIELD(o, score,      FIELD_SCORE)
#define SET_SCORE(o, v)     SET_FIELD(o, score,      FIELD_SCORE, v)
#define GET_CATEGORY(o)     GET_FIELD(o, category,   FIELD_CATEGORY)
#define SET_CATEGORY(o, v)  SET_FIELD(o, category,   FIELD_CATEGORY, v)
#define GET_TIMESTAMP(o)    GET_FIELD(o, timestamp,  FIELD_TIMESTAMP)
#define SET_TIMESTAMP(o, v) SET_FIELD(o, timestamp,  FIELD_TIMESTAMP, v)
#define GET_CHECKSUM(o)     GET_FIELD(o, checksum,   FIELD_CHECKSUM)
#define SET_CHECKSUM(o, v)  SET_FIELD(o, checksum,   FIELD_CHECKSUM, v)


/* ── APPROACH 2 — Runtime promotion ──────────────────────*/

#define EPOCH_SIZE      50000   /* check counters every N accesses  */
#define HOT_THRESHOLD   0.10    /* field is hot if > 10% of accesses */
#define MAX_OBJECTS     100000  /* max objects the system can manage */

typedef int ObjHandle;          /* integer index into handle table  */

/* Layout state of one object */
typedef enum {
    STATE_UNSPLIT  = 0,   /* all fields together, not yet promoted */
    STATE_PROMOTED = 1    /* hot fields in hot zone, cold separate  */
} ObjState;

/* The cold zone holds ALL fields (we keep it as a copy of the
 * original unsplit struct — this way cold field access always
 * works regardless of state) */
typedef struct {
    int32_t  counter;
    int32_t  flags;
    float    weight;
    float    score;
    int32_t  category;
    int32_t  version;
    int64_t  timestamp;
    int64_t  checksum;
    double   ratio;
    double   threshold;
} ColdZoneObj;

/* The hot zone holds only the frequently-accessed fields.
 * We decide which fields go here at runtime based on counters. */
typedef struct {
    int32_t  counter;    /* copied here if FIELD_COUNTER is hot  */
    int32_t  flags;      /* copied here if FIELD_FLAGS is hot    */
} HotZoneObj;

/* One entry in the handle table */
typedef struct {
    ObjState    state;
    ColdZoneObj* cold;   /* always valid — points to cold zone   */
    HotZoneObj*  hot;    /* NULL until promoted                  */
} HandleEntry;

/* The adaptive manager — owns all memory and makes decisions */
typedef struct {
    /* handle table */
    HandleEntry  handles[MAX_OBJECTS];
    int          n_objects;

    /* memory pools */
    ColdZoneObj* cold_pool;   /* flat array of cold zone objects  */
    HotZoneObj*  hot_pool;    /* flat array of hot zone objects   */
    int          hot_used;    /* how many hot slots are occupied  */

    /* per-field access counters (separate from g_tracker —
     * these reset each epoch so we track recent behavior) */
    long long    epoch_counts[FIELD_COUNT];
    long long    epoch_total;

    /* which fields are currently considered hot */
    int          is_hot[FIELD_COUNT];
    int          n_hot_fields;

    /* stats */
    int          n_promoted;
    int          n_epochs;
} AdaptiveManager;

void       adaptive_init   (AdaptiveManager* m, int n_objects);
void       adaptive_destroy(AdaptiveManager* m);

/* Allocate a new object, returns its handle */
ObjHandle  adaptive_alloc  (AdaptiveManager* m,
                             int32_t counter, int32_t flags,
                             float weight, float score,
                             int32_t category, int32_t version,
                             int64_t timestamp, int64_t checksum,
                             double ratio, double threshold);

/* Field access through handle — records to epoch counters,
 * triggers epoch check, reads from hot or cold zone */
int32_t  adaptive_get_counter  (AdaptiveManager* m, ObjHandle h);
void     adaptive_set_counter  (AdaptiveManager* m, ObjHandle h, int32_t v);
int32_t  adaptive_get_flags    (AdaptiveManager* m, ObjHandle h);
void     adaptive_set_flags    (AdaptiveManager* m, ObjHandle h, int32_t v);
float    adaptive_get_weight   (AdaptiveManager* m, ObjHandle h);
double   adaptive_get_ratio    (AdaptiveManager* m, ObjHandle h);

/* Force an epoch check right now (normally happens automatically) */
void     adaptive_epoch_check  (AdaptiveManager* m);

/* Print current state: which fields are hot, how many promoted */
void     adaptive_print_state  (AdaptiveManager* m);

/* Run the hot workload on all objects via handles */
long     adaptive_hot_workload (AdaptiveManager* m, int n,
                                int cold_every);

#endif /* ADAPTIVE_H */
