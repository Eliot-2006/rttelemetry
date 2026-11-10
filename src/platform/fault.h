/* Fault capture and reset bookkeeping (PRD §14.3). The record lives in .noinit SRAM and
 * survives warm resets (watchdog, software, fault); it is CRC-checked at boot. */
#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

#include "msg.h"            /* RESET_* codes (BOOT frame reset_reason) */

#define FAULT_MAX_TASKS 8

typedef struct {
    uint32_t reason, detail, pc, lr, psr, cfsr, hfsr, mmfar, bfar, boot_count;
    char task[16];
    char file[24];
} reset_info_t;

/* Filled by fault_boot_init(): why the previous run ended. */
extern reset_info_t fault_last_reset;

void fault_boot_init(void);
/* Register a watchdog-monitored task (priority used to name the culprit after a stall). */
void fault_register_task(unsigned id, const char *name, unsigned prio);
/* Task liveness breadcrumb: called once per loop iteration of each monitored task. */
void fault_checkin(unsigned id, uint32_t tick);
/* Mask of tasks whose last check-in is older than window_ticks relative to now. */
uint32_t fault_stale_mask(uint32_t now_tick, uint32_t window_ticks);
const char *fault_culprit_name(uint32_t stale_mask);
/* Record why we are resetting, then reset. task/file may be NULL. */
__attribute__((noreturn)) void fault_reset(uint32_t reason, uint32_t detail, const char *task, const char *file);
/* Record a watchdog stall but do not reset: the IWDG will (caller stops kicking). */
void fault_note_stall(uint32_t stale_mask);

#endif
