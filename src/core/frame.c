#include "frame.h"

#include <string.h>

#include "bytes.h"
#include "crc16.h"

size_t frame_encode(uint8_t type, uint16_t seq, uint32_t t_us, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t out_cap)
{
    if (payload_len > FRAME_PAYLOAD_MAX || out_cap == 0) return 0;
    uint8_t raw[FRAME_MAX];
    wbuf_t w = {raw, sizeof raw, 0, false};
    wb_u8(&w, FRAME_VERSION);
    wb_u8(&w, type);
    wb_u16(&w, seq);
    wb_u32(&w, t_us);
    wb_u16(&w, (uint16_t)payload_len);
    wb_bytes(&w, payload, payload_len);
    wb_u16(&w, crc16_ccitt(CRC16_INIT, raw, w.len));
    size_t n = cobs_encode(raw, w.len, out, out_cap - 1u);
    if (n == 0) return 0;
    out[n] = 0x00;
    return n + 1u;
}

frame_err_t frame_decode(const uint8_t *wire, size_t wire_len, uint8_t *scratch, size_t scratch_cap,
                         frame_hdr_t *hdr, const uint8_t **payload)
{
    /* Any COBS input longer than this decodes to more than FRAME_MAX bytes (or is malformed),
     * so reject it before decoding. */
    if (wire_len > COBS_MAX_ENCODED(FRAME_MAX)) return FRAME_ERR_OVERSIZE;
    size_t n = cobs_decode(wire, wire_len, scratch, scratch_cap);
    if (n == 0) return FRAME_ERR_COBS;
    if (n < FRAME_HDR_LEN + FRAME_CRC_LEN) return FRAME_ERR_SHORT;
    if (n > FRAME_MAX) return FRAME_ERR_OVERSIZE;

    size_t body = n - FRAME_CRC_LEN;
    uint16_t crc = (uint16_t)(scratch[body] | (scratch[body + 1] << 8));
    if (crc16_ccitt(CRC16_INIT, scratch, body) != crc) return FRAME_ERR_CRC;

    rbuf_t r = {scratch, body, 0, false};
    hdr->version = rb_u8(&r);
    hdr->type = rb_u8(&r);
    hdr->seq = rb_u16(&r);
    hdr->t_us = rb_u32(&r);
    hdr->payload_len = rb_u16(&r);
    if (hdr->version != FRAME_VERSION) return FRAME_ERR_VERSION;
    if (hdr->payload_len != body - FRAME_HDR_LEN) return FRAME_ERR_LEN;
    *payload = scratch + FRAME_HDR_LEN;
    return FRAME_OK;
}

void deframer_init(deframer_t *d) { memset(d, 0, sizeof *d); }

bool deframer_push(deframer_t *d, uint8_t byte, const uint8_t **frame, size_t *frame_len)
{
    if (byte == 0x00) {
        bool done = d->len > 0 && !d->overflow;
        if (done) {
            *frame = d->buf;
            *frame_len = d->len;
        }
        d->len = 0;
        d->overflow = false;
        return done;
    }
    if (d->overflow) return false;
    if (d->len == sizeof d->buf) {
        d->overflow = true;
        d->overflows++;
        d->len = 0;
        return false;
    }
    d->buf[d->len++] = byte;
    return false;
}
