/* Uplink command validation (PRD §12.3). Pure: decides the ACK status and extracts
 * arguments; executing the command is the firmware's job. */
#ifndef CORE_CMD_H
#define CORE_CMD_H

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "msg.h" /* ACK_* status codes */

/* FI_ARM fault ids */
enum {
    FAULT_STALL_PROC = 1,
    FAULT_TX_DELAY = 2,
    FAULT_HANG_TASK = 3,
    FAULT_UDF = 4,
    FAULT_STACK_OVF = 5,
    FAULT_ASSERT = 6,
    FAULT_DELAY_DMA_ISR = 7,
    FAULT_I2C_NACK = 8,
};

typedef struct {
    uint8_t type;
    uint16_t seq; /* frame seq of the command, echoed in the ACK */
    uint16_t rate_hz;     /* SET_RATE */
    uint8_t fault_id;     /* FI_ARM */
    uint32_t fault_param; /* FI_ARM */
} cmd_t;

/* hdr/payload come from a successful frame_decode. Fills *out (zeroed first; type and seq
 * always set) and returns an ACK_* status. Payload lengths must match exactly:
 *   PING, GET_STATUS, RESET_COUNTERS, REBOOT, FI_CLEAR: 0;  SET_RATE: u16 hz;
 *   FI_ARM: u8 fault_id, u32 param.
 * Rates: {0 (stop + flush), 100, 250, 500, 1000, 2000, 4000, 8000}.
 * FI_ARM / FI_CLEAR return ACK_UNSUPPORTED (before any other check) when !fi_enabled. */
uint8_t cmd_parse(const frame_hdr_t *hdr, const uint8_t *payload, bool fi_enabled, cmd_t *out);

#endif
