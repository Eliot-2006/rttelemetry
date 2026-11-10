#include "cmd.h"

#include <string.h>

#include "bytes.h"

static bool rate_allowed(uint16_t hz)
{
    static const uint16_t rates[] = {0, 100, 250, 500, 1000, 2000, 4000, 8000};
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++)
        if (rates[i] == hz) return true;
    return false;
}

uint8_t cmd_parse(const frame_hdr_t *hdr, const uint8_t *payload, bool fi_enabled, cmd_t *out)
{
    memset(out, 0, sizeof *out);
    out->type = hdr->type;
    out->seq = hdr->seq;
    rbuf_t r = {payload, hdr->payload_len, 0, false};

    switch (hdr->type) {
    case MSG_CMD_PING:
    case MSG_CMD_GET_STATUS:
    case MSG_CMD_RESET_COUNTERS:
    case MSG_CMD_REBOOT:
        return hdr->payload_len == 0 ? ACK_OK : ACK_ERR_LEN;
    case MSG_CMD_SET_RATE:
        if (hdr->payload_len != 2) return ACK_ERR_LEN;
        out->rate_hz = rb_u16(&r);
        return rate_allowed(out->rate_hz) ? ACK_OK : ACK_ERR_RANGE;
    case MSG_CMD_FI_ARM:
        if (!fi_enabled) return ACK_UNSUPPORTED;
        if (hdr->payload_len != 5) return ACK_ERR_LEN;
        out->fault_id = rb_u8(&r);
        out->fault_param = rb_u32(&r);
        return out->fault_id >= FAULT_STALL_PROC && out->fault_id <= FAULT_I2C_NACK ? ACK_OK : ACK_ERR_RANGE;
    case MSG_CMD_FI_CLEAR:
        if (!fi_enabled) return ACK_UNSUPPORTED;
        return hdr->payload_len == 0 ? ACK_OK : ACK_ERR_LEN;
    default:
        return ACK_ERR_UNKNOWN;
    }
}
