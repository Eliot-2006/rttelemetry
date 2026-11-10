/* Deterministic fault injection, armed over the uplink (FI_ARM). FAULT_INJECTION builds only.
 * Every fault is executed by the task that would really suffer it, at a well-defined point. */
#include "app.h"
#include "drivers.h"
#include "cmd.h"
#include "events.h"
#include "fault_inject.h"

#if FAULT_INJECTION

static _Atomic uint32_t stall_proc_ms, tx_us_per_byte, dma_isr_delay_us;
static _Atomic uint8_t hang_target = 0xFF, pending;        /* pending: one-shot cmd-task faults */
static _Atomic uint16_t armed;

/* Sacrificial task for STACK_OVF: overflow lands in the guard words in front of its stack. */
static struct { StackType_t guard[64]; StackType_t stack[128]; } fi_mem __attribute__((section(".ccm")));
static StaticTask_t fi_tcb;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"   /* deliberate: STACK_OVF */
static void recurse(volatile uint32_t depth)
{
    volatile uint8_t pad[64];
    pad[0] = (uint8_t)depth;
    vTaskDelay(1);                       /* context switch: overflow check method 2 runs here */
    recurse(depth + pad[0] + 1u);
}
#pragma GCC diagnostic pop
static void fi_task(void *arg) { (void)arg; recurse(0); }

uint8_t fi_arm(uint8_t id, uint32_t param)
{
    switch (id) {
    case FAULT_STALL_PROC:    atomic_store(&stall_proc_ms, param); break;
    case FAULT_TX_DELAY:      atomic_store(&tx_us_per_byte, param); break;
    case FAULT_DELAY_DMA_ISR: atomic_store(&dma_isr_delay_us, param); break;
    case FAULT_HANG_TASK:     if (param != TID_PROC && param != TID_CMD) return ACK_ERR_RANGE;
                           atomic_store(&hang_target, (uint8_t)param); break;
    case FAULT_UDF: case FAULT_ASSERT: case FAULT_STACK_OVF:
                           atomic_store(&pending, id); break;
    default:               return ACK_UNSUPPORTED;     /* e.g. I2C_NACK before M7 */
    }
    atomic_fetch_or(&armed, (uint16_t)(1u << id));
    log_event(EVT_FI_ARMED, ((uint32_t)id << 24) | (param & 0xFFFFFFu));
    return ACK_OK;
}

void fi_clear(void)
{
    atomic_store(&stall_proc_ms, 0); atomic_store(&tx_us_per_byte, 0); atomic_store(&dma_isr_delay_us, 0);
    atomic_store(&hang_target, 0xFF); atomic_store(&pending, 0); atomic_store(&armed, 0);
}

uint16_t fi_armed_mask(void) { return atomic_load(&armed); }

static void hang_if(unsigned tid) { if (atomic_load(&hang_target) == tid) for (;;) {} }

void fi_proc_hook(void)
{
    uint32_t ms = atomic_exchange(&stall_proc_ms, 0);
    if (ms) { vTaskDelay(pdMS_TO_TICKS(ms)); atomic_fetch_and(&armed, (uint16_t)~(1u << FAULT_STALL_PROC)); }
    hang_if(TID_PROC);
}

void fi_tx_hook(size_t bytes)
{
    uint32_t us = atomic_load(&tx_us_per_byte) * (uint32_t)bytes;
    if (us) vTaskDelay(pdMS_TO_TICKS((us + 999u) / 1000u));
}

void fi_dma_isr_hook(void)
{
    uint32_t us = atomic_load(&dma_isr_delay_us);
    if (us) { uint32_t t0 = now_us(); while (now_us() - t0 < us) {} }
}

void fi_cmd_hook(void)
{
    hang_if(TID_CMD);
    switch (atomic_exchange(&pending, 0)) {
    case FAULT_UDF:    __asm volatile(".global fi_udf_site\nfi_udf_site: udf #0"); break;
    case FAULT_ASSERT: configASSERT(0); break;
    case FAULT_STACK_OVF:
        for (unsigned i = 0; i < 64; i++) fi_mem.guard[i] = 0xDEADBEEFu;
        xTaskCreateStatic(fi_task, "fi", 128, NULL, PRIO_CMD, fi_mem.stack, &fi_tcb);
        break;
    default: break;
    }
}

#endif
