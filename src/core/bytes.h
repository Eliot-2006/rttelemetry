/* Bounds-checked little-endian writer/reader. Overflow sets `err`, never touches memory
 * outside [p, p+cap) / [p, p+len), and makes every later call a no-op. */
#ifndef CORE_BYTES_H
#define CORE_BYTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *p;
    size_t cap, len;
    bool err;
} wbuf_t;

typedef struct {
    const uint8_t *p;
    size_t len, pos;
    bool err;
} rbuf_t;

static inline void wb_bytes(wbuf_t *w, const void *src, size_t n)
{
    if (w->err || n > w->cap - w->len) {
        w->err = true;
        return;
    }
    if (n) memcpy(w->p + w->len, src, n);
    w->len += n;
}
static inline void wb_u8(wbuf_t *w, uint8_t v) { wb_bytes(w, &v, 1); }
static inline void wb_u16(wbuf_t *w, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    wb_bytes(w, b, 2);
}
static inline void wb_u32(wbuf_t *w, uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    wb_bytes(w, b, 4);
}
static inline void wb_i16(wbuf_t *w, int16_t v) { wb_u16(w, (uint16_t)v); }

/* On error the destination is zero-filled so callers never read uninitialised memory. */
static inline void rb_bytes(rbuf_t *r, void *dst, size_t n)
{
    if (r->err || n > r->len - r->pos) {
        r->err = true;
        if (n) memset(dst, 0, n);
        return;
    }
    if (n) memcpy(dst, r->p + r->pos, n);
    r->pos += n;
}
static inline uint8_t rb_u8(rbuf_t *r)
{
    uint8_t v;
    rb_bytes(r, &v, 1);
    return v;
}
static inline uint16_t rb_u16(rbuf_t *r)
{
    uint8_t b[2];
    rb_bytes(r, b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}
static inline uint32_t rb_u32(rbuf_t *r)
{
    uint8_t b[4];
    rb_bytes(r, b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline int16_t rb_i16(rbuf_t *r) { return (int16_t)rb_u16(r); }

#endif
