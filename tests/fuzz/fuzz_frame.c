/* FZ-1: frame_decode, the streaming deframer, and every msg_get_* decoder on arbitrary bytes.
 * A frame that decodes must survive re-encode + decode unchanged; a payload a msg_get_*
 * decoder accepts must re-encode to the canonical length. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "msg.h"

static void all_decoders(const uint8_t *p, size_t n)
{
    sample_rec_t s[SAMPLE_BATCH_MAX];
    imu_rec_t m[IMU_BATCH_MAX];
    event_rec_t e[EVENT_BATCH_MAX];
    boot_t b;
    health_t h;
    ack_t a;
    uint8_t count, mask;
    uint8_t out[FRAME_PAYLOAD_MAX];

    if (msg_get_sample_batch(p, n, s, &count, &mask) &&
        msg_put_sample_batch(out, sizeof out, s, count, mask) != 2u + count * SAMPLE_REC_WIRE)
        abort();
    if (msg_get_imu_batch(p, n, m, &count) && msg_put_imu_batch(out, sizeof out, m, count) != 1u + count * IMU_REC_WIRE)
        abort();
    if (msg_get_event(p, n, e, &count) && msg_put_event(out, sizeof out, e, count) != 1u + count * EVENT_REC_WIRE)
        abort();
    if (msg_get_boot(p, n, &b) && msg_put_boot(out, sizeof out, &b) != BOOT_WIRE) abort();
    if (msg_get_health(p, n, &h) && msg_put_health(out, sizeof out, &h) != HEALTH_WIRE(h.n_tasks)) abort();
    if (msg_get_ack(p, n, &a) && msg_put_ack(out, sizeof out, &a) != ACK_WIRE) abort();
}

static void one_frame(const uint8_t *wire, size_t n)
{
    uint8_t scratch[FRAME_SCRATCH_LEN];
    frame_hdr_t hdr;
    const uint8_t *payload;
    if (frame_decode(wire, n, scratch, sizeof scratch, &hdr, &payload) != FRAME_OK) return;
    all_decoders(payload, hdr.payload_len);

    /* not byte-compared with `wire`: a trailing empty block after a 0xFF block is accepted */
    uint8_t re[FRAME_WIRE_MAX], scratch2[FRAME_SCRATCH_LEN];
    size_t rn = frame_encode(hdr.type, hdr.seq, hdr.t_us, payload, hdr.payload_len, re, sizeof re);
    frame_hdr_t h2;
    const uint8_t *p2;
    if (rn == 0 || frame_decode(re, rn - 1, scratch2, sizeof scratch2, &h2, &p2) != FRAME_OK ||
        h2.type != hdr.type || h2.seq != hdr.seq || h2.t_us != hdr.t_us || h2.payload_len != hdr.payload_len ||
        memcmp(p2, payload, hdr.payload_len) != 0)
        abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    one_frame(data, size);
    all_decoders(data, size);

    deframer_t d;
    deframer_init(&d);
    for (size_t i = 0; i < size; i++) {
        const uint8_t *f;
        size_t fl;
        if (deframer_push(&d, data[i], &f, &fl)) {
            if (fl == 0 || fl > sizeof d.buf) abort();
            one_frame(f, fl);
        }
    }
    return 0;
}
