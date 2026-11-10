#include "plaus.h"

uint8_t plaus_check(const plaus_cfg_t *cfg, plaus_state_t *st, uint16_t raw)
{
    uint8_t f = 0;
    if (raw < cfg->min || raw > cfg->max) f |= PLAUS_RANGE;
    if (st->has_last && raw == st->last) {
        if (st->stuck_count != UINT16_MAX) st->stuck_count++;
    } else {
        st->stuck_count = 1;
    }
    if (st->stuck_count >= cfg->stuck_n) f |= PLAUS_STUCK;
    if (st->has_last) {
        uint16_t d = raw > st->last ? (uint16_t)(raw - st->last) : (uint16_t)(st->last - raw);
        if (d > cfg->roc_max) f |= PLAUS_ROC;
    }
    st->last = raw;
    st->has_last = true;
    return f;
}
