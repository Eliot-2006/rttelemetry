#include "fault.h"
#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "crc16.h"
#include "stm32f4xx.h"

#define REC_MAGIC     0x7E1E0001u
#define REASON_RUNNING 0xFFu         /* set at boot; still present at next boot => watchdog/pin reset */

typedef struct {
    uint32_t magic;
    reset_info_t info;
    uint32_t crc;
} reset_record_t;

typedef struct {
    uint32_t magic;
    const char *name[FAULT_MAX_TASKS];
    uint8_t prio[FAULT_MAX_TASKS];
    uint32_t registered;
    volatile uint32_t tick[FAULT_MAX_TASKS];
} alive_table_t;

__attribute__((section(".noinit"))) static reset_record_t rec;
__attribute__((section(".noinit"))) static alive_table_t alive;

reset_info_t fault_last_reset;

static uint32_t rec_crc(void) { return crc16_ccitt(CRC16_INIT, &rec.info, sizeof rec.info); }
static void rec_seal(void) { rec.magic = REC_MAGIC; rec.crc = rec_crc(); }
static void copy_str(char *dst, size_t cap, const char *src)
{
    memset(dst, 0, cap);
    if (src) strncpy(dst, src, cap - 1);
}

uint32_t fault_stale_mask(uint32_t now_tick, uint32_t window_ticks)
{
    uint32_t m = 0;
    for (unsigned i = 0; i < FAULT_MAX_TASKS; i++)
        if ((alive.registered & (1u << i)) && now_tick - alive.tick[i] > window_ticks) m |= 1u << i;
    return m;
}

const char *fault_culprit_name(uint32_t stale_mask)
{
    /* Busy-looping task starves everything below it: the culprit is the highest-priority stale task. */
    int best = -1;
    for (unsigned i = 0; i < FAULT_MAX_TASKS; i++)
        if ((stale_mask & (1u << i)) && (best < 0 || alive.prio[i] > alive.prio[best])) best = (int)i;
    return best < 0 ? NULL : alive.name[best];
}

void fault_boot_init(void)
{
    bool valid = rec.magic == REC_MAGIC && rec.crc == rec_crc();
    if (!valid) {
        memset(&rec, 0, sizeof rec);
        rec.info.reason = RESET_POWER_ON;
    } else if (rec.info.reason == REASON_RUNNING) {
        /* Nothing recorded a reason: the IWDG (or reset pin) fired while tasks were running.
         * Reconstruct the stall from the liveness breadcrumbs. */
        uint32_t newest = 0;
        for (unsigned i = 0; i < FAULT_MAX_TASKS; i++)
            if ((alive.registered & (1u << i)) && alive.tick[i] > newest) newest = alive.tick[i];
        uint32_t stale = alive.magic == REC_MAGIC ? fault_stale_mask(newest, 500) : 0;
        rec.info.reason = stale ? RESET_WDT_TASK_STALL : RESET_UNKNOWN_WDT;
        rec.info.detail = stale;
        copy_str(rec.info.task, sizeof rec.info.task, stale ? fault_culprit_name(stale) : NULL);
    }
    fault_last_reset = rec.info;
    fault_last_reset.boot_count = rec.info.boot_count + 1u;

    uint32_t boots = rec.info.boot_count + 1u;
    memset(&rec.info, 0, sizeof rec.info);
    rec.info.reason = REASON_RUNNING;
    rec.info.boot_count = boots;
    rec_seal();
    memset(&alive, 0, sizeof alive);
    alive.magic = REC_MAGIC;
}

void fault_register_task(unsigned id, const char *name, unsigned prio)
{
    alive.name[id] = name;
    alive.prio[id] = (uint8_t)prio;
    alive.tick[id] = 0;
    alive.registered |= 1u << id;
}

void fault_checkin(unsigned id, uint32_t tick) { alive.tick[id] = tick; }

void fault_note_stall(uint32_t stale_mask)
{
    rec.info.reason = RESET_WDT_TASK_STALL;
    rec.info.detail = stale_mask;
    copy_str(rec.info.task, sizeof rec.info.task, fault_culprit_name(stale_mask));
    rec_seal();
}

void fault_reset(uint32_t reason, uint32_t detail, const char *task, const char *file)
{
    __disable_irq();
    rec.info.reason = reason;
    rec.info.detail = detail;
    copy_str(rec.info.task, sizeof rec.info.task, task);
    if (file) {                                   /* keep the tail: the file name is what matters */
        size_t n = strlen(file);
        copy_str(rec.info.file, sizeof rec.info.file, n >= sizeof rec.info.file ? file + n - (sizeof rec.info.file - 1) : file);
    }
    rec_seal();
    NVIC_SystemReset();
}

void fault_assert(const char *file, int line)
{
    TaskHandle_t t = xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ? NULL : xTaskGetCurrentTaskHandle();
    fault_reset(RESET_ASSERT, (uint32_t)line, t ? pcTaskGetName(t) : "boot", file);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    fault_reset(RESET_STACK_OVF, 0, name, NULL);
}

/* HardFault/UsageFault/BusFault/MemManage: capture the stacked frame and fault status. */
__attribute__((used)) void fault_exception_c(const uint32_t *frame)
{
    rec.info.pc = frame[6];
    rec.info.lr = frame[5];
    rec.info.psr = frame[7];
    rec.info.cfsr = SCB->CFSR;
    rec.info.hfsr = SCB->HFSR;
    rec.info.mmfar = SCB->MMFAR;
    rec.info.bfar = SCB->BFAR;
    TaskHandle_t t = xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ? NULL : xTaskGetCurrentTaskHandle();
    fault_reset(RESET_HARDFAULT, SCB->CFSR, t ? pcTaskGetName(t) : "isr", NULL);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b fault_exception_c\n");
}
__attribute__((naked)) void UsageFault_Handler(void) { __asm volatile("b HardFault_Handler\n"); }
__attribute__((naked)) void BusFault_Handler(void)   { __asm volatile("b HardFault_Handler\n"); }
__attribute__((naked)) void MemManage_Handler(void)  { __asm volatile("b HardFault_Handler\n"); }
