/* FZ-2: the uplink trust boundary, frame_decode -> cmd_parse. Because a valid CRC is hard to
 * hit blindly, the input is also handed to cmd_parse directly: data[0] = type, rest = payload. */
#include <stdint.h>
#include <stdlib.h>

#include "cmd.h"

static void check(const frame_hdr_t *hdr, const uint8_t *payload)
{
    for (int fi = 0; fi < 2; fi++) {
        cmd_t c;
        uint8_t st = cmd_parse(hdr, payload, fi, &c);
        if (st > ACK_BUSY || c.type != hdr->type || c.seq != hdr->seq) abort();
        if (st == ACK_OK && hdr->type == MSG_CMD_FI_ARM && (!fi || c.fault_id < FAULT_STALL_PROC || c.fault_id > FAULT_I2C_NACK))
            abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t scratch[FRAME_SCRATCH_LEN];
    frame_hdr_t hdr;
    const uint8_t *payload;
    if (frame_decode(data, size, scratch, sizeof scratch, &hdr, &payload) == FRAME_OK) check(&hdr, payload);

    if (size >= 1 && size - 1 <= FRAME_PAYLOAD_MAX) {
        frame_hdr_t h = {FRAME_VERSION, data[0], 0x5A5A, 0, (uint16_t)(size - 1)};
        check(&h, data + 1);
    }
    return 0;
}
