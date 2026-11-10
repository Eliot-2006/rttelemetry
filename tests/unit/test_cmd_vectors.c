/* XV (C side): decode command frames encoded by the Python tool (tests/vectors/cmd_*.bin,
 * kept in sync by tools/test_vectors.py) with deframer + frame_decode + cmd_parse. */
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    const char *file;
    uint8_t type;
    uint16_t seq;
    uint8_t status;
    uint16_t rate_hz;
    uint8_t fault_id;
    uint32_t fault_param;
} vec_t;

static const vec_t vecs[] = {
    {"cmd_ping.bin", MSG_CMD_PING, 1, ACK_OK, 0, 0, 0},
    {"cmd_get_status.bin", MSG_CMD_GET_STATUS, 2, ACK_OK, 0, 0, 0},
    {"cmd_set_rate_1000.bin", MSG_CMD_SET_RATE, 3, ACK_OK, 1000, 0, 0},
    {"cmd_set_rate_0.bin", MSG_CMD_SET_RATE, 4, ACK_OK, 0, 0, 0},
    {"cmd_reset_counters.bin", MSG_CMD_RESET_COUNTERS, 5, ACK_OK, 0, 0, 0},
    {"cmd_reboot.bin", MSG_CMD_REBOOT, 6, ACK_OK, 0, 0, 0},
    {"cmd_fi_arm.bin", MSG_CMD_FI_ARM, 7, ACK_OK, 0, FAULT_STALL_PROC, 500},
    {"cmd_fi_clear.bin", MSG_CMD_FI_CLEAR, 8, ACK_OK, 0, 0, 0},
    {"cmd_set_rate_bad.bin", MSG_CMD_SET_RATE, 0xFFFF, ACK_ERR_RANGE, 123, 0, 0},
};

static void test_python_encoded_commands(void)
{
    for (size_t i = 0; i < sizeof vecs / sizeof vecs[0]; i++) {
        const vec_t *v = &vecs[i];
        char path[512];
        snprintf(path, sizeof path, "%s/%s", VECTORS_DIR, v->file);
        FILE *f = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
        uint8_t bytes[FRAME_WIRE_MAX + 8];
        size_t n = fread(bytes, 1, sizeof bytes, f);
        fclose(f);

        deframer_t d;
        deframer_init(&d);
        const uint8_t *w = NULL;
        size_t wl = 0;
        int frames = 0;
        for (size_t k = 0; k < n; k++) frames += deframer_push(&d, bytes[k], &w, &wl);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, frames, v->file);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, bytes[n - 1], v->file);

        uint8_t scratch[FRAME_SCRATCH_LEN];
        frame_hdr_t h;
        const uint8_t *p;
        TEST_ASSERT_EQUAL_INT_MESSAGE(FRAME_OK, frame_decode(w, wl, scratch, sizeof scratch, &h, &p), v->file);
        cmd_t c;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->status, cmd_parse(&h, p, true, &c), v->file);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->type, c.type, v->file);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->seq, c.seq, v->file);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->rate_hz, c.rate_hz, v->file);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(v->fault_id, c.fault_id, v->file);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(v->fault_param, c.fault_param, v->file);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_python_encoded_commands);
    return UNITY_END();
}
