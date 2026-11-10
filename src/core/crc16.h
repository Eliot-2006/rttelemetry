/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
 * Check value: "123456789" -> 0x29B1. Python: binascii.crc_hqx(data, 0xFFFF). */
#ifndef CORE_CRC16_H
#define CORE_CRC16_H

#include <stddef.h>
#include <stdint.h>

#define CRC16_INIT 0xFFFFu

/* Pass CRC16_INIT for the first chunk, then the previous result to continue. */
uint16_t crc16_ccitt(uint16_t crc, const void *data, size_t len);

#endif
