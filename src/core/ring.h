/* Lock-free single-producer/single-consumer ring buffer (PRD §9).
 * Free-running u32 indices; all `capacity` slots usable; drop-newest on full. */
#ifndef CORE_RING_H
#define CORE_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    _Atomic uint32_t head;  /* next write index; written ONLY by producer */
    _Atomic uint32_t tail;  /* next read index;  written ONLY by consumer */
    uint8_t *buf;           /* capacity * elem_size bytes, caller-provided (static) */
    uint32_t capacity;      /* power of two, <= 2^31 */
    uint32_t elem_size;
    _Atomic uint32_t drops; /* written ONLY by producer */
    _Atomic uint32_t hwm;   /* occupancy high-water mark; written ONLY by producer */
} ring_t;

/* false if buf is NULL, elem_size is 0, or capacity is not a power of two in [1, 2^31]. */
bool ring_init(ring_t *r, void *buf, uint32_t capacity, uint32_t elem_size);
bool ring_push(ring_t *r, const void *elem); /* producer only; false + drops++ if full */
bool ring_pop(ring_t *r, void *elem);        /* consumer only; false if empty */
uint32_t ring_count(const ring_t *r);        /* any context; snapshot, may be stale */

#endif
