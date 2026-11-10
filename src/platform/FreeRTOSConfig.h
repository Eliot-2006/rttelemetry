/* FreeRTOS V11 configuration for STM32F407 @ 168 MHz (PRD §8.3). Static allocation only. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#define configCPU_CLOCK_HZ                        168000000UL
#define configTICK_RATE_HZ                        1000
#define configUSE_PREEMPTION                      1
#define configUSE_TIME_SLICING                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1
#define configUSE_TICKLESS_IDLE                   0
#define configMAX_PRIORITIES                      8
#define configMINIMAL_STACK_SIZE                  128
#define configMAX_TASK_NAME_LEN                   8
#define configTICK_TYPE_WIDTH_IN_BITS             TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                   1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1
#define configQUEUE_REGISTRY_SIZE                 0
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configUSE_NEWLIB_REENTRANT                0

/* Memory: no heap at all (NFR-MEM-1). */
#define configSUPPORT_STATIC_ALLOCATION           1
#define configSUPPORT_DYNAMIC_ALLOCATION          0
#define configKERNEL_PROVIDED_STATIC_MEMORY       1

/* Hooks and diagnostics */
#define configUSE_IDLE_HOOK                       1
#define configUSE_TICK_HOOK                       0
#define configCHECK_FOR_STACK_OVERFLOW            2
#define configUSE_MALLOC_FAILED_HOOK              0
#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      0
#define configGENERATE_RUN_TIME_STATS             1
#define configRUN_TIME_COUNTER_TYPE               uint32_t
extern volatile uint32_t *const rt_counter;    /* TIM2->CNT: free-running 1 MHz */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  /* TIM2 is started before the scheduler */
#define portGET_RUN_TIME_COUNTER_VALUE()          (*rt_counter)

/* Primitives used */
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               0
#define configUSE_COUNTING_SEMAPHORES             0
#define configUSE_QUEUE_SETS                      0
#define configUSE_TASK_NOTIFICATIONS              1
#define configUSE_TIMERS                          0
#define configUSE_CO_ROUTINES                     0
#define configUSE_STREAM_BUFFERS                  0

#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetIdleTaskHandle            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_xTaskGetSchedulerState            1

/* Cortex-M4 interrupt priorities: 4 implemented bits. ISRs at numeric priority < 5 (e.g. the
 * sample-trigger TIM3 at 4) are never masked by the kernel and must not call any RTOS API. */
#define configPRIO_BITS                           4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

void fault_assert(const char *file, int line);
#define configASSERT(x) do { if (!(x)) fault_assert(__FILE__, __LINE__); } while (0)

/* Map port handlers onto the CMSIS vector names used by the ST startup file. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
