/* UT-FRAME (PRD §19.2), incl. the streaming deframer. */
#include <string.h>

#include "cobs.h"
#include "crc16.h"
#include "frame.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t wire[600], scratch[FRAME_SCRATCH_LEN], raw[600];
static frame_hdr_t hdr;
static const uint8_t *pl;

/* Build header||payload||crc by hand (independent of frame_encode), return COBS length. */
static size_t craft(uint8_t version, uint8_t type, uint16_t seq, uint32_t t, uint16_t hdr_len,
                    const uint8_t *payload, size_t n, int crc_delta)
{
    raw[0] = version;
    raw[1] = type;
    raw[2] = (uint8_t)seq;
    raw[3] = (uint8_t)(seq >> 8);
    raw[4] = (uint8_t)t;
    raw[5] = (uint8_t)(t >> 8);
    raw[6] = (uint8_t)(t >> 16);
    raw[7] = (uint8_t)(t >> 24);
    raw[8] = (uint8_t)hdr_len;
    raw[9] = (uint8_t)(hdr_len >> 8);
    if (n) memcpy(raw + 10, payload, n);
    uint16_t c = (uint16_t)(crc16_ccitt(CRC16_INIT, raw, 10 + n) + crc_delta);
    raw[10 + n] = (uint8_t)c;
    raw[11 + n] = (uint8_t)(c >> 8);
    return cobs_encode(raw, 12 + n, wire, sizeof wire);
}

static frame_err_t dec(size_t wire_len)
{
    return frame_decode(wire, wire_len, scratch, sizeof scratch, &hdr, &pl);
}

static void test_encode_decode_each_type(void)
{
    const uint8_t types[] = {MSG_SAMPLE_BATCH, MSG_IMU_BATCH, MSG_BOOT,       MSG_HEALTH,
                             MSG_EVENT,        MSG_ACK,       MSG_CMD_PING,   MSG_CMD_GET_STATUS,
                             MSG_CMD_SET_RATE, MSG_CMD_REBOOT, MSG_CMD_FI_ARM, MSG_CMD_FI_CLEAR};
    uint8_t payload[FRAME_PAYLOAD_MAX];
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i % 7 == 0 ? 0 : i);
    for (size_t i = 0; i < sizeof types; i++) {
        size_t n = (i * 41) % (FRAME_PAYLOAD_MAX + 1);
        size_t w = frame_encode(types[i], (uint16_t)(1000 + i), 0xFEDCBA98u + (uint32_t)i, payload, n, wire,
                                sizeof wire);
        TEST_ASSERT_TRUE(w > 0 && w <= FRAME_WIRE_MAX);
        TEST_ASSERT_EQUAL_HEX8(0x00, wire[w - 1]);
        TEST_ASSERT_NULL(memchr(wire, 0, w - 1));
        TEST_ASSERT_EQUAL_INT(FRAME_OK, dec(w - 1));
        TEST_ASSERT_EQUAL_UINT8(FRAME_VERSION, hdr.version);
        TEST_ASSERT_EQUAL_UINT8(types[i], hdr.type);
        TEST_ASSERT_EQUAL_UINT16(1000 + i, hdr.seq);
        TEST_ASSERT_EQUAL_HEX32(0xFEDCBA98u + (uint32_t)i, hdr.t_us);
        TEST_ASSERT_EQUAL_UINT16(n, hdr.payload_len);
        if (n) TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, pl, n);
    }
}

static void test_encode_matches_hand_crafted(void)
{
    const uint8_t p[] = {1, 0, 2, 0, 0, 3};
    size_t c = craft(1, MSG_ACK, 0x1234, 0x89ABCDEFu, 6, p, 6, 0);
    uint8_t out[64];
    size_t w = frame_encode(MSG_ACK, 0x1234, 0x89ABCDEFu, p, 6, out, sizeof out);
    TEST_ASSERT_EQUAL_size_t(c + 1, w);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(wire, out, c);
}

static void test_empty_payload_null_ok(void)
{
    size_t w = frame_encode(MSG_CMD_PING, 0, 0, NULL, 0, wire, sizeof wire);
    TEST_ASSERT_TRUE(w > 0);
    TEST_ASSERT_EQUAL_INT(FRAME_OK, dec(w - 1));
    TEST_ASSERT_EQUAL_UINT16(0, hdr.payload_len);
}

static void test_encode_errors(void)
{
    uint8_t payload[FRAME_PAYLOAD_MAX + 1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, frame_encode(1, 0, 0, payload, FRAME_PAYLOAD_MAX + 1, wire, sizeof wire));
    size_t w = frame_encode(1, 0, 0, payload, FRAME_PAYLOAD_MAX, wire, sizeof wire);
    TEST_ASSERT_TRUE(w > 0 && w <= FRAME_WIRE_MAX);
    for (size_t cap = 0; cap < w; cap++) TEST_ASSERT_EQUAL_size_t(0, frame_encode(1, 0, 0, payload, FRAME_PAYLOAD_MAX, wire, cap));
    /* worst case (no zeros) still fits FRAME_WIRE_MAX */
    memset(payload, 0x55, sizeof payload);
    TEST_ASSERT_TRUE(frame_encode(0x55, 0x5555, 0x55555555u, payload, FRAME_PAYLOAD_MAX, wire, FRAME_WIRE_MAX) > 0);
}

static void test_reject_cobs(void)
{
    const uint8_t bad[] = {0x05, 0x01, 0x02};
    memcpy(wire, bad, sizeof bad);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_COBS, dec(sizeof bad));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_COBS, dec(0));
}

static void test_reject_short(void)
{
    uint8_t r11[11] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    size_t w = cobs_encode(r11, 11, wire, sizeof wire);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_SHORT, dec(w));
    w = cobs_encode(r11, 1, wire, sizeof wire);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_SHORT, dec(w));
    /* exactly 12 bytes is a valid empty frame */
    w = craft(1, MSG_CMD_PING, 0, 0, 0, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_OK, dec(w));
}

static void test_reject_oversize(void)
{
    uint8_t payload[300];
    memset(payload, 0x11, sizeof payload);
    /* decoded 257 bytes */
    size_t w = craft(1, 1, 0, 0, FRAME_PAYLOAD_MAX + 1, payload, FRAME_PAYLOAD_MAX + 1, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, dec(w));
    /* decoded 257 bytes in a 258-byte wire (zeros shorten the encoding) */
    memset(payload, 0, sizeof payload);
    w = craft(1, 1, 0, 0, FRAME_PAYLOAD_MAX + 1, payload, FRAME_PAYLOAD_MAX + 1, 0);
    TEST_ASSERT_TRUE(w <= COBS_MAX_ENCODED(FRAME_MAX));
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, dec(w));
    /* 0xFF-heavy: wire longer than COBS_MAX_ENCODED(FRAME_MAX) */
    memset(raw, 0x11, 400);
    w = cobs_encode(raw, 400, wire, sizeof wire);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, dec(w));
    /* huge garbage */
    memset(wire, 0x22, sizeof wire);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_OVERSIZE, dec(sizeof wire));
}

static void test_reject_crc(void)
{
    const uint8_t p[] = {9, 8, 7};
    size_t w = craft(1, MSG_ACK, 1, 2, 3, p, 3, 1);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, dec(w));
    /* any single bit flip of the pre-COBS frame is detected */
    for (size_t bit = 0; bit < (12 + 3) * 8; bit++) {
        craft(1, MSG_ACK, 1, 2, 3, p, 3, 0);
        raw[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        w = cobs_encode(raw, 15, wire, sizeof wire);
        TEST_ASSERT_NOT_EQUAL(FRAME_OK, dec(w));
    }
}

static void test_reject_version(void)
{
    size_t w = craft(2, MSG_CMD_PING, 0, 0, 0, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_VERSION, dec(w));
    w = craft(0, MSG_CMD_PING, 0, 0, 0, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_VERSION, dec(w));
}

static void test_reject_len_mismatch(void)
{
    const uint8_t p[] = {1, 2, 3, 4};
    size_t w = craft(1, MSG_ACK, 0, 0, 3, p, 4, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_LEN, dec(w));
    w = craft(1, MSG_ACK, 0, 0, 5, p, 4, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_LEN, dec(w));
    w = craft(1, MSG_ACK, 0, 0, 0xFFFF, p, 4, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_LEN, dec(w));
}

static void test_validation_order(void)
{
    /* bad CRC and bad version and bad length: CRC wins */
    size_t w = craft(7, 1, 0, 0, 99, NULL, 0, 1);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_CRC, dec(w));
    /* bad version and bad length: version wins */
    w = craft(7, 1, 0, 0, 99, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_VERSION, dec(w));
}

static void test_scratch_too_small_is_cobs_error(void)
{
    size_t w = craft(1, 1, 0, 0, 0, NULL, 0, 0);
    TEST_ASSERT_EQUAL_INT(FRAME_ERR_COBS, frame_decode(wire, w, scratch, 11, &hdr, &pl));
}

static void test_seq_wrap(void)
{
    const uint16_t seqs[] = {0xFFFE, 0xFFFF, 0x0000, 0x0001};
    for (size_t i = 0; i < 4; i++) {
        size_t w = frame_encode(MSG_ACK, seqs[i], 0xFFFFFFFFu, NULL, 0, wire, sizeof wire);
        TEST_ASSERT_EQUAL_INT(FRAME_OK, dec(w - 1));
        TEST_ASSERT_EQUAL_UINT16(seqs[i], hdr.seq);
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, hdr.t_us);
    }
}

/* ---- deframer ---- */

static deframer_t d;

static int feed(const uint8_t *b, size_t n, const uint8_t **f, size_t *fl)
{
    int got = 0;
    for (size_t i = 0; i < n; i++) got += deframer_push(&d, b[i], f, fl);
    return got;
}

static void test_deframer_splits_and_decodes(void)
{
    uint8_t stream[3 * FRAME_WIRE_MAX];
    size_t n = 0;
    for (uint16_t s = 0; s < 3; s++) n += frame_encode(MSG_CMD_PING, s, s, NULL, 0, stream + n, sizeof stream - n);
    deframer_init(&d);
    uint16_t expect = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *f;
        size_t fl;
        if (deframer_push(&d, stream[i], &f, &fl)) {
            TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(f, fl, scratch, sizeof scratch, &hdr, &pl));
            TEST_ASSERT_EQUAL_UINT16(expect++, hdr.seq);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(3, expect);
}

static void test_deframer_ignores_empty_frames(void)
{
    const uint8_t z[] = {0, 0, 0};
    const uint8_t *f;
    size_t fl;
    deframer_init(&d);
    TEST_ASSERT_EQUAL_INT(0, feed(z, 3, &f, &fl));
    const uint8_t one[] = {0x42, 0x00};
    TEST_ASSERT_EQUAL_INT(1, feed(one, 2, &f, &fl));
    TEST_ASSERT_EQUAL_size_t(1, fl);
    TEST_ASSERT_EQUAL_HEX8(0x42, f[0]);
}

static void test_deframer_overflow_discards_until_delimiter(void)
{
    const uint8_t *f;
    size_t fl;
    deframer_init(&d);
    uint8_t junk[FRAME_WIRE_MAX + 50];
    memset(junk, 0x33, sizeof junk);
    TEST_ASSERT_EQUAL_INT(0, feed(junk, sizeof junk, &f, &fl));
    TEST_ASSERT_EQUAL_UINT32(1, d.overflows);
    const uint8_t z = 0;
    TEST_ASSERT_EQUAL_INT(0, feed(&z, 1, &f, &fl)); /* overflowed frame not reported */
    size_t w = frame_encode(MSG_CMD_PING, 77, 0, NULL, 0, wire, sizeof wire);
    TEST_ASSERT_EQUAL_INT(1, feed(wire, w, &f, &fl)); /* next frame intact */
    TEST_ASSERT_EQUAL_INT(FRAME_OK, frame_decode(f, fl, scratch, sizeof scratch, &hdr, &pl));
    TEST_ASSERT_EQUAL_UINT16(77, hdr.seq);
    TEST_ASSERT_EQUAL_UINT32(1, d.overflows);
}

static void test_deframer_exact_buffer_fits(void)
{
    const uint8_t *f;
    size_t fl;
    deframer_init(&d);
    uint8_t junk[FRAME_WIRE_MAX];
    memset(junk, 0x44, sizeof junk);
    TEST_ASSERT_EQUAL_INT(0, feed(junk, sizeof junk, &f, &fl));
    const uint8_t z = 0;
    TEST_ASSERT_EQUAL_INT(1, feed(&z, 1, &f, &fl));
    TEST_ASSERT_EQUAL_size_t(FRAME_WIRE_MAX, fl);
    TEST_ASSERT_EQUAL_UINT32(0, d.overflows);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_decode_each_type);
    RUN_TEST(test_encode_matches_hand_crafted);
    RUN_TEST(test_empty_payload_null_ok);
    RUN_TEST(test_encode_errors);
    RUN_TEST(test_reject_cobs);
    RUN_TEST(test_reject_short);
    RUN_TEST(test_reject_oversize);
    RUN_TEST(test_reject_crc);
    RUN_TEST(test_reject_version);
    RUN_TEST(test_reject_len_mismatch);
    RUN_TEST(test_validation_order);
    RUN_TEST(test_scratch_too_small_is_cobs_error);
    RUN_TEST(test_seq_wrap);
    RUN_TEST(test_deframer_splits_and_decodes);
    RUN_TEST(test_deframer_ignores_empty_frames);
    RUN_TEST(test_deframer_overflow_discards_until_delimiter);
    RUN_TEST(test_deframer_exact_buffer_fits);
    return UNITY_END();
}
