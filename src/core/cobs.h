/* Consistent Overhead Byte Stuffing (Cheshire & Baker). The 0x00 delimiter is NOT
 * part of the encoding; frame.c appends it. */
#ifndef CORE_COBS_H
#define CORE_COBS_H

#include <stddef.h>
#include <stdint.h>

#define COBS_MAX_ENCODED(n) ((n) + ((n) / 254u) + 1u)

/* Returns encoded length (>= 1), or 0 if cap is too small. Output never contains 0x00. */
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);
/* Returns decoded length, or 0 if input is malformed (0x00 inside, a code that runs past
 * the end) or does not fit in cap. An empty frame (encoding 0x01) also returns 0. */
size_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

#endif
