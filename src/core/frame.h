/* Wire framing (PRD §12.1):
 *   wire := COBS(header || payload || crc16_le) || 0x00
 *   header (10 B, LE): u8 version, u8 type, u16 seq, u32 t_us, u16 payload_len */
#ifndef CORE_FRAME_H
#define CORE_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cobs.h"

#define FRAME_VERSION 1u
#define FRAME_HDR_LEN 10u
#define FRAME_CRC_LEN 2u
#define FRAME_MAX 256u /* pre-COBS header + payload + crc */
#define FRAME_PAYLOAD_MAX (FRAME_MAX - FRAME_HDR_LEN - FRAME_CRC_LEN)
#define FRAME_WIRE_MAX (COBS_MAX_ENCODED(FRAME_MAX) + 1u) /* incl. 0x00 delimiter */
/* frame_decode scratch size: one byte over FRAME_MAX so an oversize frame is reported as
 * FRAME_ERR_OVERSIZE rather than failing the COBS decode for lack of room. */
#define FRAME_SCRATCH_LEN (FRAME_MAX + 1u)

enum {
    MSG_SAMPLE_BATCH = 0x01,
    MSG_IMU_BATCH = 0x02,
    MSG_BOOT = 0x10,
    MSG_HEALTH = 0x11,
    MSG_EVENT = 0x12,
    MSG_ACK = 0x20,
    MSG_CMD_PING = 0x80,
    MSG_CMD_GET_STATUS = 0x81,
    MSG_CMD_SET_RATE = 0x82,
    MSG_CMD_RESET_COUNTERS = 0x83,
    MSG_CMD_REBOOT = 0x84,
    MSG_CMD_FI_ARM = 0x90,
    MSG_CMD_FI_CLEAR = 0x91,
};

typedef struct {
    uint8_t version, type;
    uint16_t seq;
    uint32_t t_us;
    uint16_t payload_len;
} frame_hdr_t;

/* Rejection reasons, in validation order. */
typedef enum {
    FRAME_OK = 0,
    FRAME_ERR_COBS,     /* malformed COBS (or decoded frame does not fit scratch_cap) */
    FRAME_ERR_SHORT,    /* decoded length < header + crc */
    FRAME_ERR_OVERSIZE, /* longer than FRAME_MAX (checked on wire length first, then decoded) */
    FRAME_ERR_CRC,
    FRAME_ERR_VERSION,
    FRAME_ERR_LEN, /* header payload_len != actual payload length */
} frame_err_t;

/* Writes the complete wire frame, including the trailing 0x00, into out.
 * Returns its length, or 0 if payload_len > FRAME_PAYLOAD_MAX or out_cap is too small.
 * Uses FRAME_MAX bytes of stack. */
size_t frame_encode(uint8_t type, uint16_t seq, uint32_t t_us, const uint8_t *payload,
                    size_t payload_len, uint8_t *out, size_t out_cap);

/* wire excludes the 0x00 delimiter. scratch should hold FRAME_SCRATCH_LEN bytes (a smaller
 * scratch turns frames that do not fit into FRAME_ERR_COBS). On FRAME_OK,
 * *payload points into scratch (hdr->payload_len bytes). Type-specific payload length is
 * checked later by msg_get_* / cmd_parse. */
frame_err_t frame_decode(const uint8_t *wire, size_t wire_len, uint8_t *scratch, size_t scratch_cap,
                         frame_hdr_t *hdr, const uint8_t **payload);

/* Streaming splitter for a byte stream (uplink RX). */
typedef struct {
    uint8_t buf[FRAME_WIRE_MAX];
    size_t len;
    bool overflow;      /* discarding until the next delimiter */
    uint32_t overflows; /* frames discarded because they exceeded buf */
} deframer_t;

void deframer_init(deframer_t *d);
/* Returns true when `byte` is a delimiter that completes a non-empty, non-overflowed frame;
 * *frame / *frame_len then describe it (without delimiter), valid until the next push. */
bool deframer_push(deframer_t *d, uint8_t byte, const uint8_t **frame, size_t *frame_len);

#endif
