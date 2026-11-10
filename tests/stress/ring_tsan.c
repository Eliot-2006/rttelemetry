/* ST-RING-1: one producer thread, one consumer thread, real parallelism (run under TSan).
 * The consumer checks every element is intact, seq strictly increases, and that the number
 * of missing seqs equals the ring's drop counter.
 * usage: ring_tsan [items=10000000] [slow]   ("slow" = consumer yields per pop, forcing drops) */
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ring.h"

typedef struct {
    uint32_t seq;
    uint32_t check[4]; /* derived from seq: detects torn or stale slot reads */
} item_t;

static ring_t ring;
static item_t storage[64];
static uint32_t n_items;
static int slow;
static atomic_bool producer_done;

static uint32_t mix(uint32_t s, uint32_t k) { return s * 2654435761u + k * 0x9E3779B9u; }

static void *producer(void *arg)
{
    (void)arg;
    for (uint32_t s = 0; s < n_items; s++) {
        item_t it = {.seq = s};
        for (uint32_t k = 0; k < 4; k++) it.check[k] = mix(s, k);
        (void)ring_push(&ring, &it); /* drop-newest: failure counted in ring.drops */
    }
    atomic_store_explicit(&producer_done, true, memory_order_release);
    return NULL;
}

int main(int argc, char **argv)
{
    n_items = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 10000000u;
    slow = argc > 2 && strcmp(argv[2], "slow") == 0;
    if (!ring_init(&ring, storage, slow ? 16 : 64, sizeof(item_t))) return 2;

    pthread_t th;
    if (pthread_create(&th, NULL, producer, NULL) != 0) return 2;

    uint64_t popped = 0, gaps = 0;
    int64_t last = -1;
    item_t it;
    for (;;) {
        /* read the flag before popping: if it was set, the ring holds everything left */
        bool done = atomic_load_explicit(&producer_done, memory_order_acquire);
        if (!ring_pop(&ring, &it)) {
            if (done) break;
            continue;
        }
        for (uint32_t k = 0; k < 4; k++) {
            if (it.check[k] != mix(it.seq, k)) {
                fprintf(stderr, "FAIL: torn element seq=%u\n", it.seq);
                return 1;
            }
        }
        if ((int64_t)it.seq <= last) {
            fprintf(stderr, "FAIL: seq not increasing: %u after %lld\n", it.seq, (long long)last);
            return 1;
        }
        gaps += (uint64_t)((int64_t)it.seq - last - 1);
        last = it.seq;
        popped++;
        if (slow) sched_yield();
    }
    pthread_join(th, NULL);
    gaps += (uint64_t)((int64_t)n_items - 1 - last); /* trailing drops */

    uint32_t drops = atomic_load(&ring.drops);
    printf("[HOST] items=%u popped=%llu gaps=%llu drops=%u hwm=%u capacity=%u%s\n", n_items,
           (unsigned long long)popped, (unsigned long long)gaps, drops, atomic_load(&ring.hwm), ring.capacity,
           slow ? " (slow consumer)" : "");
    if (gaps != drops || popped + drops != n_items) {
        fprintf(stderr, "FAIL: gaps != drops or popped + drops != items\n");
        return 1;
    }
    if (slow && drops == 0) {
        fprintf(stderr, "FAIL: slow consumer produced no drops; overflow path not exercised\n");
        return 1;
    }
    return 0;
}
