#include "hotcold.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ObjectUnsplit* build_unsplit(int n) {
    ObjectUnsplit* arr = (ObjectUnsplit*)malloc(sizeof(ObjectUnsplit) * (size_t)n);
    for (int i = 0; i < n; i++) {
        arr[i].counter   = i;
        arr[i].flags     = i % 256;
        arr[i].weight    = (float)i * 0.1f;
        arr[i].score     = (float)i * 0.2f;
        arr[i].category  = i % 16;
        arr[i].version   = 1;
        arr[i].timestamp = (int64_t)i * 1000;
        arr[i].checksum  = (int64_t)i * 31;
        arr[i].ratio     = (double)i / (double)(n + 1);
        arr[i].threshold = 0.5;
    }
    return arr;
}

void free_unsplit(ObjectUnsplit* arr) {
    free(arr);
}

ObjectSplit* build_split_hot(int n) {
    ObjectSplit* hot = (ObjectSplit*)malloc(sizeof(ObjectSplit) * (size_t)n);
    for (int i = 0; i < n; i++) {
        hot[i].counter = i;
        hot[i].flags   = i % 256;
        hot[i].cold    = NULL;
    }
    return hot;
}

ColdData* build_split_cold(int n, ObjectSplit* hot) {
    ColdData* cold = (ColdData*)malloc(sizeof(ColdData) * (size_t)n);
    for (int i = 0; i < n; i++) {
        cold[i].weight    = (float)i * 0.1f;
        cold[i].score     = (float)i * 0.2f;
        cold[i].category  = i % 16;
        cold[i].version   = 1;
        cold[i].timestamp = (int64_t)i * 1000;
        cold[i].checksum  = (int64_t)i * 31;
        cold[i].ratio     = (double)i / (double)(n + 1);
        cold[i].threshold = 0.5;
        hot[i].cold       = &cold[i];
    }
    return cold;
}

void free_split(ObjectSplit* hot, ColdData* cold) {
    free(hot);
    free(cold);
}

ObjectPadded* build_padded(int n) {
    ObjectPadded* arr = (ObjectPadded*)malloc(sizeof(ObjectPadded) * (size_t)n);
    for (int i = 0; i < n; i++) {
        arr[i].counter   = i;
        arr[i].flags     = i % 256;
        arr[i].weight    = (float)i * 0.1f;
        arr[i].score     = (float)i * 0.2f;
        arr[i].category  = i % 16;
        arr[i].version   = 1;
        arr[i].timestamp = (int64_t)i * 1000;
        arr[i].checksum  = (int64_t)i * 31;
        arr[i].ratio     = (double)i / (double)(n + 1);
        arr[i].threshold = 0.5;
    }
    return arr;
}

void free_padded(ObjectPadded* arr) {
    free(arr);
}


long workload_unsplit(ObjectUnsplit* arr, int n, int cold_every) {
    long sum = 0;
    for (int i = 0; i < n; i++) {
        arr[i].counter++;
        arr[i].flags ^= 0x01;
        if (cold_every > 0 && i % cold_every == 0)
            sum += (long)arr[i].weight;
    }
    return sum + arr[0].counter;
}

long workload_split(ObjectSplit* arr, int n, int cold_every) {
    long sum = 0;
    for (int i = 0; i < n; i++) {
        arr[i].counter++;
        arr[i].flags ^= 0x01;
        if (cold_every > 0 && i % cold_every == 0)
            sum += (long)arr[i].cold->weight;
    }
    return sum + arr[0].counter;
}

long workload_padded(ObjectPadded* arr, int n, int cold_every) {
    long sum = 0;
    for (int i = 0; i < n; i++) {
        arr[i].counter++;
        arr[i].flags ^= 0x01;
        if (cold_every > 0 && i % cold_every == 0)
            sum += (long)arr[i].weight;
    }
    return sum + arr[0].counter;
}


void print_struct_sizes(void) {
    printf("  Struct sizes\n");
    printf("  ────────────────────────────────────────────\n");
    printf("  sizeof(ObjectUnsplit) = %3zu bytes  (%zu cache lines per object)\n",
           sizeof(ObjectUnsplit),
           (sizeof(ObjectUnsplit) + 63) / 64);
    printf("  sizeof(ObjectSplit)   = %3zu bytes  (hot part only)\n",
           sizeof(ObjectSplit));
    printf("  sizeof(ColdData)      = %3zu bytes\n",
           sizeof(ColdData));
    printf("  sizeof(ObjectPadded)  = %3zu bytes\n",
           sizeof(ObjectPadded));
    printf("\n");
    printf("  Objects fitting in one 64-byte cache line\n");
    printf("  ────────────────────────────────────────────\n");
    printf("  Unsplit : %zu object(s)  (loads hot + cold fields always)\n",
           64 / sizeof(ObjectUnsplit) == 0 ? 1 : 64 / sizeof(ObjectUnsplit));
    printf("  Split   : %zu object(s)  (hot part only; cold never loaded)\n",
           64 / sizeof(ObjectSplit));
    printf("  Padded  : %zu object(s)  (same as Unsplit, reorder doesn't help)\n",
           64 / sizeof(ObjectPadded) == 0 ? 1 : 64 / sizeof(ObjectPadded));
    printf("\n");
}
