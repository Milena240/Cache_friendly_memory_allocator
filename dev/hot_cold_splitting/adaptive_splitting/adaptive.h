#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include <stdint.h>
#include <stddef.h>

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

extern const char* FIELD_NAMES[FIELD_COUNT];


typedef struct {
    long long counts[FIELD_COUNT];
    long long total_accesses;
    long long epoch_accesses;
} FieldTracker;

extern FieldTracker g_tracker;

void tracker_init  (FieldTracker* t);
void tracker_reset (FieldTracker* t);

static inline void tracker_record(FieldTracker* t, FieldID fid) {
    t->counts[fid]++;
    t->total_accesses++;
    t->epoch_accesses++;
}

void print_field_report(FieldTracker* t, const char* title);


#define PROFILING 1

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

#if PROFILING
  #define GET_FIELD(obj, fname, fid) \
      (tracker_record(&g_tracker, fid), (obj)->fname)

  #define SET_FIELD(obj, fname, fid, val) \
      (tracker_record(&g_tracker, fid), (obj)->fname = (val))
#else
  #define GET_FIELD(obj, fname, fid)        ((obj)->fname)
  #define SET_FIELD(obj, fname, fid, val)   ((obj)->fname = (val))
#endif

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


#define EPOCH_SIZE      50000
#define HOT_THRESHOLD   0.10
#define MAX_OBJECTS     100000

typedef int ObjHandle;

typedef enum {
    STATE_UNSPLIT  = 0,
    STATE_PROMOTED = 1
} ObjState;

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

typedef struct {
    int32_t  counter;
    int32_t  flags;
} HotZoneObj;

typedef struct {
    ObjState    state;
    ColdZoneObj* cold;
    HotZoneObj*  hot;
} HandleEntry;

typedef struct {
    HandleEntry  handles[MAX_OBJECTS];
    int          n_objects;

    ColdZoneObj* cold_pool;
    HotZoneObj*  hot_pool;
    int          hot_used;

    long long    epoch_counts[FIELD_COUNT];
    long long    epoch_total;

    int          is_hot[FIELD_COUNT];
    int          n_hot_fields;

    int          n_promoted;
    int          n_epochs;
} AdaptiveManager;

void       adaptive_init   (AdaptiveManager* m, int n_objects);
void       adaptive_destroy(AdaptiveManager* m);

ObjHandle  adaptive_alloc  (AdaptiveManager* m,
                             int32_t counter, int32_t flags,
                             float weight, float score,
                             int32_t category, int32_t version,
                             int64_t timestamp, int64_t checksum,
                             double ratio, double threshold);

int32_t  adaptive_get_counter  (AdaptiveManager* m, ObjHandle h);
void     adaptive_set_counter  (AdaptiveManager* m, ObjHandle h, int32_t v);
int32_t  adaptive_get_flags    (AdaptiveManager* m, ObjHandle h);
void     adaptive_set_flags    (AdaptiveManager* m, ObjHandle h, int32_t v);
float    adaptive_get_weight   (AdaptiveManager* m, ObjHandle h);
double   adaptive_get_ratio    (AdaptiveManager* m, ObjHandle h);

void     adaptive_epoch_check  (AdaptiveManager* m);
void     adaptive_print_state  (AdaptiveManager* m);
long     adaptive_hot_workload (AdaptiveManager* m, int n, int cold_every);

#endif /* ADAPTIVE_H */
