#ifndef HOTCOLD_H
#define HOTCOLD_H

#include <stdint.h>
#include <stddef.h>

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
} ObjectUnsplit;


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
    int32_t   counter;
    int32_t   flags;
    ColdData* cold;
} ObjectSplit;


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
} ObjectPadded;


ObjectUnsplit* build_unsplit (int n);
void           free_unsplit  (ObjectUnsplit* arr);

ObjectSplit*   build_split_hot  (int n);
ColdData*      build_split_cold (int n, ObjectSplit* hot);
void           free_split       (ObjectSplit* hot, ColdData* cold);

ObjectPadded*  build_padded (int n);
void           free_padded  (ObjectPadded* arr);


long workload_unsplit (ObjectUnsplit* arr, int n, int cold_every);
long workload_split   (ObjectSplit*   arr, int n, int cold_every);
long workload_padded  (ObjectPadded*  arr, int n, int cold_every);

void print_struct_sizes(void);

#endif /* HOTCOLD_H */
