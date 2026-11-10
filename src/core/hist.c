#include "hist.h"

const uint32_t HIST_EDGES_US[HIST_BUCKETS] = {500, 1000, 2000, 5000, 10000, 20000, 50000, UINT32_MAX};

void hist_add(hist_t *h, uint32_t us)
{
    unsigned i = 0;
    while (us > HIST_EDGES_US[i]) i++; /* terminates: last edge is UINT32_MAX */
    if (h->b[i] != UINT32_MAX) h->b[i]++;
    if (us > h->max) h->max = us;
}
