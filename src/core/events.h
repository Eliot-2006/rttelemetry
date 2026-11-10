/* Event IDs carried in EVENT frames (PRD §13). Single source of truth: the host tool
 * (tools/telemetry/events.py) parses the enumerator lines below, so keep exactly one
 * `EVT_<NAME> = <id>,` per line followed by a one-line comment. Append only; never renumber. */
#ifndef CORE_EVENTS_H
#define CORE_EVENTS_H

enum {
    EVT_BOOT = 1,              /* firmware booted; arg = boot_count */
    EVT_RESET_REASON = 2,      /* reason for the previous reset; arg = RESET_* code */
    EVT_RING_OVF_ON = 3,       /* sample ring started dropping; arg = drops so far */
    EVT_RING_OVF_OFF = 4,      /* sample ring stopped dropping; arg = drops during the episode */
    EVT_TXPOOL_EMPTY_ON = 5,   /* frame pool exhausted, batches dropped; arg = dropped_txpool so far */
    EVT_TXPOOL_EMPTY_OFF = 6,  /* frame pool available again; arg = samples dropped in the episode */
    EVT_ADC_OVERRUN = 7,       /* trigger fired while a scan was in flight; arg = dropped_overrun */
    EVT_SENSOR_FLAG_ON = 8,    /* plausibility flag set; arg = (channel << 8) | PLAUS_* bit */
    EVT_SENSOR_FLAG_OFF = 9,   /* plausibility flag cleared; arg = (channel << 8) | PLAUS_* bit */
    EVT_CMD_REJECTED = 10,     /* uplink command rejected; arg = (type << 8) | ACK status */
    EVT_RATE_CHANGED = 11,     /* sample rate changed; arg = new rate in Hz */
    EVT_FI_ARMED = 12,         /* fault injection armed; arg = fault id */
    EVT_WDT_STALL = 13,        /* liveness check failed, watchdog will reset; arg = missing mask */
    EVT_TX_TIMEOUT = 14,       /* UART TX DMA did not complete in time; arg = tx_timeouts */
    EVT_I2C_ERROR = 15,        /* I2C transaction failed; arg = driver error code */
    EVT_I2C_RECOVERED = 16,    /* I2C bus recovered, sensor back online; arg = attempts */
    EVT_IMU_TIMEOUT = 17,      /* no IMU data-ready for 5 periods; arg = imu_samples */
};

#endif
