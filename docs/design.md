# Design

This file covers the firmware architecture as built, the invariants it relies on, and the Renode
divergence register. The requirements are in `docs/PRD.md`, the wire format in `docs/protocol.md`,
and the test strategy in `docs/testing.md`.

## Data flow

```
 TIM3 update (prio 4, no RTOS calls)
   seq++, overrun? ──yes──> dropped_overrun++
   │ no: stamp t_us (TIM2, 1 MHz), SWSTART
   ▼
 ADC1 scan IN1, IN8, IN16, IN17 ──DMA2_Stream0 (normal mode, 32-bit)──> scan_buf (SRAM1)
   ▼
 DMA2_Stream0 TC (prio 5)            ring full? ──> sample_ring.drops (dropped_ring)
   build sample_rec_t ──ring_push──> sample_ring (SPSC, 128) ──notify──> proc_task (prio 5)
                                                                            │ plausibility flags, events
                                                                            │ batch 10 / flush 20 ms
                                     pool empty? ──> dropped_txpool          ▼
 imu_task (prio 6, 5 ms poll) ─IMU_BATCH─┐                       telemetry_send() (non-blocking)
 health_task (prio 2) ─BOOT/HEALTH/EVENT─┤                                  │
 cmd_task (prio 3) ─ACK───────────────────┴──> frame pool (4 × 244 B) ──tx_q──> tx_task (prio 4)
                                                                            │ COBS + CRC into tx_wire,
                                                                            │ frame seq in wire order
                                                                            ▼
                                                    USART2 TX via DMA1_Stream6 (921600 baud)
 USART2 RX (prio 7) ──> rx_ring (512 B) ──notify on 0x00 or half-full──> cmd_task: deframe, decode,
                                                                          cmd_parse, execute, ACK
```

The same `telemetry.elf` is built for silicon and runs unmodified in Renode. Inputs are injected at
the peripheral-model level (ADC feed files, LSM6DSO samples), so the real driver code runs in both.

## Execution contexts

| Context | Priority | Job | Blocking |
|---|---|---|---|
| TIM3 IRQ | 4 (above `configMAX_SYSCALL_INTERRUPT_PRIORITY` = 5) | Trigger scan, count overruns | Never; no RTOS API |
| DMA2_Stream0 IRQ | 5 | Sample record → ring, notify proc | Never |
| I2C / IMU EXTI | 6 | Unused: the IMU is polled (D11) | — |
| DMA1_Stream6, USART2 IRQ | 7 | TX complete notify; RX byte → rx_ring | Never |
| `imu_task` | 6 | 14-byte burst every 5 ms, IMU_BATCH | Polled I²C, bounded spins |
| `proc_task` | 5 | Plausibility, batching, flush | Notification, 10 ms timeout |
| `tx_task` | 4 | Encode + DMA, frame seq | Queue; TX-complete notify with timeout |
| `cmd_task` | 3 | Uplink commands | Notification, 100 ms timeout |
| `health_task` | 2 | BOOT, HEALTH 1 Hz, events, watchdog supervisor | 100 ms slots + GET_STATUS notify |
| idle | 0 | `WFI` | — |

All kernel objects and stacks are static (`configSUPPORT_DYNAMIC_ALLOCATION 0`). `_sbrk` traps, and
`scripts/check_no_heap.sh` fails the build if an allocator is linked.

## Memory map

| Region | Contents | Notes |
|---|---|---|
| FLASH | code, rodata, `.data` init | 23 KB release, 31 KB debug (`make size-check`) |
| SRAM1/2 | `.data`, `.bss` (TCBs, ring, pool, `scan_buf`, `tx_wire`), `.noinit` reset record | Every DMA buffer is here. `DMA_ADDR_OK()` asserts it at init (D4) |
| CCM | `.msp` (2 KiB MSP stack at the bottom: an overflow faults instead of corrupting), task stacks | No DMA on silicon |

## Invariants

- **I-ACQ-1** `t_trig`/`cur_seq` are written only by TIM3 while `in_flight == 0`, and read only by the DMA ISR while it is 1. The DMA ISR clears `in_flight` with release ordering after reading them. TIM3 at priority 4 can preempt the DMA ISR, and this ordering keeps that safe.
- **I-RING** The ring is SPSC with free-running u32 indices: the producer is the DMA ISR, the consumer is `proc_task`. Each index is written by one side only. Drops are counted by the producer. TSan stress test: `make test-tsan`.
- **I-POOL-1** Each pool index is in exactly one place: `free_q`, a producer between receive and send, or `tx_q`. Both queues have depth `POOL_N`, so a send can never fail (asserted).
- **Conservation** `triggered = delivered + dropped_overrun + dropped_ring + dropped_txpool + in_flight`. It is checked as a bound at every HEALTH frame and exactly after `SET_RATE 0` (PRD §18). Loss-accounting counters are never reset.
- **Single writer per counter.** Cross-context counters are C11 atomics. The multi-word latency histogram is guarded by `stats_mutex`, which has priority inheritance.

## Fault handling

| Fault | Detection | Action |
|---|---|---|
| Task hang (proc, tx, cmd, imu) | Each task checks in through `alive()`. `health_task` checks a 500 ms window | Stops kicking the IWDG (1 s), logs WDT_STALL, records the stale mask. If `health_task` itself is starved, the boot code reconstructs the culprit from the `.noinit` liveness table |
| HardFault / Usage / Bus / MemManage | Naked handlers select MSP/PSP from `EXC_RETURN` | Stacked PC/LR/PSR plus CFSR/HFSR/MMFAR/BFAR go into the `.noinit` record, then reset |
| Stack overflow | `configCHECK_FOR_STACK_OVERFLOW 2` | The record names the task, then reset |
| `configASSERT` | `fault_assert` | The record holds file and line, then reset |
| Ring / pool / overrun | Counters + edge events | Degrade: drop newest, keep running |
| I²C error | Two retries | Bus recovery (9 SCL clocks + peripheral reset), sensor OFFLINE, `WHO_AM_I` re-probe at 1 Hz |
| Malformed uplink | COBS/CRC/length/args | `rx_frames_bad` or an error ACK. Never resets |

The reset record (magic + CRC16, `.noinit`) is reported in the BOOT frame of the next boot.

## Renode divergence register

D1–D9 are from the PRD (source review). D10–D15 were found by tests in M0–M3 (evidence in
`docs/platform-verification.md` and `docs/progress.md`).

| ID | Divergence (Renode 1.17.0) | Guard |
|---|---|---|
| D1 | UART TX instantaneous; baud not modeled | Link metrics are `[CALC]`/`[HW]`; FI-02 emulates a slow link (B-05) |
| D2 | No circular / half-transfer DMA | Normal-mode DMA re-armed per scan |
| D3 | No ADC external trigger | Software trigger from the TIM3 ISR |
| D4 | DMA reaches any address, incl. CCM | DMA buffers in SRAM; `DMA_ADDR_OK()` asserted |
| D5 | Unmapped accesses read 0, no BusFault | Fault injection uses `UDF`; bus faults are `[HW]` only |
| D6 | `RCC_CSR` reset flags constant | `.noinit` reset record |
| D7 | Calibration ROM reads 0 | Raw counts on the wire; calibration in BOOT; host falls back to nominal values |
| D8 | Not cycle-accurate | `[EMU]` vs `[HW]` labels everywhere |
| D9 | ADC sampling time not modeled | No `[EMU]` timing claim depends on it |
| D10 | ADC model is 32-bit access only | ADC DMA uses 32-bit peripheral and memory width |
| D11 | LSM6DSO: no DRDY interrupt, `STATUS_REG` unhandled | `imu_task` polls every 5 ms |
| D12 | I²C model ignores ACK/POS; back-to-back N=1 reads can stick | Production code avoids N=1 after N=1; silicon-correct sequences kept |
| D13 | ADC conversion fixed at 100 µs/channel (400 µs per scan) | Emulated rates ≥ ~2.5 kHz overrun deterministically and are counted |
| D14 | DMA stream `EN` not cleared at transfer complete | TX TC handler clears `EN` (no-op on silicon) |
| D15 | Timer update every `ARR` ticks (silicon `ARR + 1`) | Firmware keeps the silicon-correct `ARR`; tests expect `period − 1` in EMU |

## Decisions

- **Custom SPSC ring instead of a FreeRTOS queue** for ISR → proc. It needs no critical section, it counts drops at the producer, and it has a host-testable portable core (PRD §9.6). The cycle comparison (B-03) needs hardware.
- **The tx task owns encoding** and returns the pool entry before DMA starts, so frame sequence numbers follow wire order.
- **`RESET_COUNTERS` clears diagnostics only.** Conservation covers the whole run.
- **The IMU is polled** (D11), and `imu_task` sends its own IMU_BATCH frames instead of going through `proc_task` and `imu_q`. Drop accounting is unchanged.
- **The MSP sits at the bottom of CCM**, so an MSP overflow faults.
- **EVENT batches are retried** when the pool is empty, so a TXPOOL_EMPTY_ON event is not lost in the episode it reports.
- **Software-triggered ADC** on both targets (D3). The timer-TRGO variant is a `[HW]`-only improvement.
