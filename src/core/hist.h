/* Fixed log-ish latency histogram (µs). Bucket i counts values <= HIST_EDGES_US[i]
 * (and > the previous edge). Counts saturate at UINT32_MAX. */
#ifndef CORE_HIST_H
#define CORE_HIST_H

#include <stdint.h>

#define HIST_BUCKETS 8

extern const uint32_t HIST_EDGES_US[HIST_BUCKETS]; /* 500 1000 2000 5000 10000 20000 50000 UINT32_MAX */

typedef struct {
    uint32_t b[HIST_BUCKETS];
    uint32_t max;
} hist_t;

void hist_add(hist_t *h, uint32_t us);

#endif
