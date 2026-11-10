/* Per-channel plausibility checks (PRD §11). Pure functions; one plaus_state_t per channel. */
#ifndef CORE_PLAUS_H
#define CORE_PLAUS_H

#include <stdbool.h>
#include <stdint.h>

#define PLAUS_RANGE 1u /* raw < min || raw > max */
#define PLAUS_STUCK 2u /* this sample is the stuck_n-th (or later) identical consecutive value; stuck_n 0 = off */
#define PLAUS_ROC 4u   /* |raw - previous raw| > roc_max */

/* Per-sample status word: 4 bits per channel, channel 0 in the low nibble. */
#define PLAUS_STATUS(ch, flags) ((uint16_t)((flags) << (4 * (ch))))

typedef struct {
    uint16_t min, max, roc_max, stuck_n;
} plaus_cfg_t;

typedef struct {
    uint16_t last, stuck_count;
    bool has_last;
} plaus_state_t; /* zero-initialise before the first sample */

uint8_t plaus_check(const plaus_cfg_t *cfg, plaus_state_t *st, uint16_t raw);

#endif
