/* Fault injection hooks (PRD §19.4). Compiled to no-ops unless FAULT_INJECTION=1. */
#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include <stddef.h>
#include <stdint.h>

#ifndef FAULT_INJECTION
#define FAULT_INJECTION 0
#endif

#if FAULT_INJECTION
uint8_t fi_arm(uint8_t fault_id, uint32_t param);   /* returns ACK status */
void fi_clear(void);
uint16_t fi_armed_mask(void);
void fi_proc_hook(void);       /* proc_task loop: STALL_PROC, HANG_TASK(proc) */
void fi_tx_hook(size_t bytes); /* tx_task per frame: TX_DELAY */
void fi_cmd_hook(void);        /* cmd_task loop: HANG_TASK(cmd), UDF, ASSERT, STACK_OVF */
void fi_dma_isr_hook(void);    /* ADC DMA ISR: DELAY_DMA_ISR */
#else
static inline uint16_t fi_armed_mask(void) { return 0; }
static inline void fi_proc_hook(void) {}
static inline void fi_tx_hook(size_t bytes) { (void)bytes; }
static inline void fi_cmd_hook(void) {}
static inline void fi_dma_isr_hook(void) {}
#endif

#endif
