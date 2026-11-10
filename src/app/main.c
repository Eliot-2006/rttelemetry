/* Boot: fault record, clocks, drivers, kernel objects, tasks (all static), scheduler. */
#include "app.h"
#include "drivers.h"
#include "fault.h"

counters_t ctr;
SemaphoreHandle_t stats_mutex;
hist_t l1_hist;
TaskHandle_t h_proc, h_tx, h_cmd, h_health;
volatile uint32_t *const rt_counter = &TIM2->CNT;

/* Task stacks live in CCM (CPU-only RAM); TCBs and every DMA buffer stay in SRAM. */
#define STACK(name, words) static StackType_t name[words] __attribute__((section(".ccm")))
STACK(proc_stack, 384);
STACK(tx_stack, 384);
STACK(cmd_stack, 384);
STACK(health_stack, 384);
static StaticTask_t proc_tcb, tx_tcb, cmd_tcb, health_tcb;
static StaticSemaphore_t stats_mutex_buf;

extern uint32_t __msp_limit;

/* Paint the unused MSP area so HEALTH can report its high-water mark. */
static void msp_paint(void)
{
    uint32_t *p = &__msp_limit;
    const uint32_t *sp = (const uint32_t *)__get_MSP() - 16;    /* stay clear of the current frame */
    while (p < sp) *p++ = 0xA5A5A5A5u;
}

#define CREATE(fn, name, prio, stack, tcb, handle, tid)                                       \
    do {                                                                                     \
        handle = xTaskCreateStatic(fn, name, sizeof(stack) / sizeof(StackType_t), NULL, prio, \
                                   stack, &tcb);                                             \
        fault_register_task(tid, name, prio);                                                \
    } while (0)

int main(void)
{
    fault_boot_init();
    msp_paint();
    configASSERT(clock_init());
    markers_init();
    tim2_timebase_init();
    uart_init(TELEM_BAUD);
    adc_init();

    stats_mutex = xSemaphoreCreateMutexStatic(&stats_mutex_buf);
    telemetry_init();
    events_init();
    CREATE(proc_task, "proc", PRIO_PROC, proc_stack, proc_tcb, h_proc, TID_PROC);
    CREATE(tx_task, "tx", PRIO_TX, tx_stack, tx_tcb, h_tx, TID_TX);
    CREATE(cmd_task, "cmd", PRIO_CMD, cmd_stack, cmd_tcb, h_cmd, TID_CMD);
    CREATE(health_task, "health", PRIO_HEALTH, health_stack, health_tcb, h_health, TID_HEALTH);
    vTaskStartScheduler();
    for (;;) {}
}

void vApplicationIdleHook(void) { __WFI(); }
