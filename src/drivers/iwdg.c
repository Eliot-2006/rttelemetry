#include "drivers.h"

void iwdg_start(uint32_t timeout_ms)
{
    if (timeout_ms < 1u) timeout_ms = 1u;
    if (timeout_ms > 4095u) timeout_ms = 4095u;
    IWDG->KR = 0x5555u;            /* unlock PR/RLR */
    IWDG->PR = 3u;                 /* LSI 32 kHz / 32 = 1 kHz (nominal; silicon LSI is 17-47 kHz) */
    IWDG->RLR = timeout_ms - 1u;
    IWDG->KR = 0xAAAAu;
    IWDG->KR = 0xCCCCu;            /* start (also enables LSI) */
}
