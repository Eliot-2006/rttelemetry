/* LSM6DSO IMU on I2C1 (PRD §10.3, P1), polled (divergence D11: Renode 1.17.0 models no DRDY
 * interrupt). imu_task reads a 14-byte burst (OUT_TEMP_L..OUTZ_H_A) every IMU_PERIOD_MS and sends
 * IMU_BATCH frames of IMU_BATCH_MAX records itself; a full frame pool drops the batch (imu_dropped).
 *
 * Errors (PRD §14.1): a failed read is retried twice, then the bus is recovered (9 SCL clocks +
 * peripheral reset) and the sensor goes OFFLINE; WHO_AM_I is re-probed once per second. The ADC
 * path is unaffected throughout. On silicon a burst busy-waits ~0.4 ms [CALC] at priority 6. */
#include <stdatomic.h>
#include "app.h"
#include "drivers.h"
#include "fault_inject.h"

#define IMU_ADDR       0x6Au
#define REG_WHO_AM_I   0x0Fu
#define WHO_AM_I_VAL   0x6Cu
#define REG_CTRL1_XL   0x10u
#define REG_CTRL2_G    0x11u
#define REG_CTRL3_C    0x12u
#define REG_OUT_TEMP_L 0x20u
#define CTRL1_XL_208HZ_4G   0x58u   /* ODR_XL 0101 = 208 Hz, FS_XL 10 = +-4 g */
#define CTRL2_G_208HZ_500DPS 0x54u  /* ODR_G 0101 = 208 Hz, FS_G 01 = 500 dps */
#define CTRL3_C_BDU_IF_INC  0x44u   /* block data update + register auto-increment */
#define IMU_PERIOD_MS  5u           /* 200 Hz poll of the 208 Hz ODR (1 ms tick) */
#define PROBE_PERIOD_MS 1000u

_Atomic uint32_t imu_samples, imu_dropped, i2c_errors;
_Atomic uint8_t imu_status;          /* IMU_ABSENT / IMU_ONLINE / IMU_OFFLINE (docs/protocol.md) */

static imu_rec_t batch[IMU_BATCH_MAX];
static uint8_t batch_n;
static uint8_t payload[FRAME_PAYLOAD_MAX];

static i2c_status_t xfer_read(uint8_t reg, uint8_t *dst, size_t n)
{
    // cppcheck-suppress knownConditionTrueFalse ; constant false only in release (no fault injection)
    if (fi_i2c_hook()) return I2C_ERR_NACK;           /* FI-12 */
    return i2c_read_reg(IMU_ADDR, reg, dst, n);
}

static bool probe(void)
{
    uint8_t who[2];                                   /* N=2 read (D12: avoid back-to-back N=1) */
    if (xfer_read(REG_WHO_AM_I, who, 2) != I2C_OK || who[0] != WHO_AM_I_VAL) return false;
    return i2c_write_reg(IMU_ADDR, REG_CTRL3_C, CTRL3_C_BDU_IF_INC) == I2C_OK &&
           i2c_write_reg(IMU_ADDR, REG_CTRL1_XL, CTRL1_XL_208HZ_4G) == I2C_OK &&
           i2c_write_reg(IMU_ADDR, REG_CTRL2_G, CTRL2_G_208HZ_500DPS) == I2C_OK;
}

static void flush(void)
{
    size_t len = msg_put_imu_batch(payload, sizeof payload, batch, batch_n);
    configASSERT(len > 0);
    if (!telemetry_send(MSG_IMU_BATCH, payload, len, now_us())) atomic_fetch_add(&imu_dropped, batch_n);
    batch_n = 0;
}

static bool read_sample(uint32_t seq)
{
    uint8_t b[14];
    uint32_t t = now_us();
    i2c_status_t s = I2C_OK;
    for (unsigned attempt = 0; attempt < 3u; attempt++) {
        if ((s = xfer_read(REG_OUT_TEMP_L, b, sizeof b)) == I2C_OK) break;
        atomic_fetch_add(&i2c_errors, 1u);
    }
    if (s != I2C_OK) {
        log_event(EVT_I2C_ERROR, s);
        i2c_bus_recover();
        return false;
    }
    imu_rec_t *r = &batch[batch_n++];
    r->seq = seq;
    r->t_us = t;
    r->temp = (int16_t)(b[0] | b[1] << 8);
    for (unsigned i = 0; i < 3; i++) {
        r->gyr[i] = (int16_t)(b[2 + 2 * i] | b[3 + 2 * i] << 8);
        r->acc[i] = (int16_t)(b[8 + 2 * i] | b[9 + 2 * i] << 8);
    }
    atomic_fetch_add(&imu_samples, 1u);
    if (batch_n == IMU_BATCH_MAX) flush();
    return true;
}

void imu_task(void *arg)
{
    (void)arg;
    i2c_init();
    uint32_t seq = 0, attempts = 0, since_probe = PROBE_PERIOD_MS;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(IMU_PERIOD_MS));
        alive(TID_IMU);
        if (atomic_load(&imu_status) != IMU_ONLINE) {
            since_probe += IMU_PERIOD_MS;
            if (since_probe < PROBE_PERIOD_MS) continue;
            since_probe = 0;
            attempts++;
            if (!probe()) {
                atomic_store(&imu_status, IMU_OFFLINE);
                continue;
            }
            if (attempts > 1u) log_event(EVT_I2C_RECOVERED, attempts);
            attempts = 0;
            atomic_store(&imu_status, IMU_ONLINE);
        }
        if (!read_sample(seq++)) {
            if (batch_n) flush();                     /* keep what was read before the fault */
            atomic_store(&imu_status, IMU_OFFLINE);
            since_probe = 0;
            attempts = 1;
        }
    }
}
