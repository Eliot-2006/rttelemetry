/* SPSC ring, PRD §9.2. The release store of an index publishes the slot bytes written
 * before it; the matching acquire load on the other side makes them visible. */
#include "ring.h"

#include <string.h>

bool ring_init(ring_t *r, void *buf, uint32_t capacity, uint32_t elem_size)
{
    if (buf == NULL || elem_size == 0 || capacity == 0 || capacity > 0x80000000u ||
        (capacity & (capacity - 1u)) != 0)
        return false;
    atomic_init(&r->head, 0);
    atomic_init(&r->tail, 0);
    atomic_init(&r->drops, 0);
    atomic_init(&r->hwm, 0);
    r->buf = buf;
    r->capacity = capacity;
    r->elem_size = elem_size;
    return true;
}

static uint8_t *slot(const ring_t *r, uint32_t idx)
{
    return r->buf + (size_t)(idx & (r->capacity - 1u)) * r->elem_size;
}

bool ring_push(ring_t *r, const void *elem)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t == r->capacity) {
        /* producer is the only writer, so load+store is not a lost update */
        atomic_store_explicit(&r->drops, atomic_load_explicit(&r->drops, memory_order_relaxed) + 1u,
                              memory_order_relaxed);
        return false;
    }
    memcpy(slot(r, h), elem, r->elem_size);
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);
    uint32_t occ = h + 1u - t; /* t may be stale: an upper bound, which is what a HWM wants */
    if (occ > atomic_load_explicit(&r->hwm, memory_order_relaxed))
        atomic_store_explicit(&r->hwm, occ, memory_order_relaxed);
    return true;
}

bool ring_pop(ring_t *r, void *elem)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return false;
    memcpy(elem, slot(r, t), r->elem_size);
    atomic_store_explicit(&r->tail, t + 1u, memory_order_release);
    return true;
}

uint32_t ring_count(const ring_t *r)
{
    /* tail first: head only grows, so the difference cannot go negative. Both may move
     * between the loads, so clamp to capacity. */
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    uint32_t n = h - t;
    return n > r->capacity ? r->capacity : n;
}
