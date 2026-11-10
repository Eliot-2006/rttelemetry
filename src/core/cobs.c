#include "cobs.h"

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (cap == 0) return 0;
    size_t code_at = 0, o = 1; /* out[code_at] receives the current block's code */
    uint8_t code = 1;
    for (size_t i = 0; i < len; i++) {
        if (in[i] != 0) {
            if (o >= cap) return 0;
            out[o++] = in[i];
            if (++code != 0xFF) continue;
            if (i + 1 == len) break; /* full block at the very end: no trailing code byte */
        }
        out[code_at] = code; /* block ends at a zero or after 254 data bytes */
        code = 1;
        if (o >= cap) return 0;
        code_at = o++;
    }
    out[code_at] = code;
    return o;
}

size_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    size_t i = 0, o = 0;
    while (i < len) {
        uint8_t code = in[i++];
        if (code == 0 || code - 1u > len - i) return 0;
        for (uint8_t k = 1; k < code; k++) {
            if (in[i] == 0 || o >= cap) return 0;
            out[o++] = in[i++];
        }
        if (code != 0xFF && i < len) {
            if (o >= cap) return 0;
            out[o++] = 0;
        }
    }
    return o;
}
