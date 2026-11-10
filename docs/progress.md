# Progress and evidence log

Milestones from `docs/PRD.md` §21. Every result below is `[EMU]` (Renode 1.17.0 virtual time) or
`[HOST]` unless marked otherwise. **No physical hardware has been used.** Nothing here is a
hardware measurement.

| Milestone | State | Evidence |
|---|---|---|
| M0 platform bring-up | Done | `docs/platform-verification.md`, `make test-platform` (G1–G10 PASS) |
| M1 portable core + host tests + Python decoder | Done | `make test-host test-tsan test-py`, `make coverage`, `make fuzz`, `docs/protocol.md` |
| M2 RTOS firmware skeleton (tasks, HEALTH, BOOT, watchdog supervisor) | Done | `tests/robot/telemetry.robot` IT-BOOT-1, IT-HEALTH-1 (G3: HEALTH spacing 1 000 000 µs ± 1 ms) |
| M3 acquisition, processing, commands, loss accounting | Done | IT-ACQ-1/2/3, IT-CMD-1, IT-ACCT-1, FI-03, FI-04 |
| M4/M5 fault injection and recovery | Pending | |
| M6 benchmarks | Pending | |
| M7 IMU (P1) | Pending | IT-HEALTH-1 stack check needs `imu_task` |
| M8 response-time analysis | Tool done | `tools/rta.py`, `docs/scheduling.md` (WCET inputs are placeholders until M6) |
| M9 hardware-in-the-loop | **Not possible: no hardware** | |
| M10 docs | Pending | |

## Divergences found after M0 (continuing D10–D13 in `docs/platform-verification.md`)

| ID | Divergence (Renode 1.17.0) | Consequence / handling |
|---|---|---|
| D14 | `STM32DMA`: a stream's `EN` bit stays set after transfer complete (silicon clears it in hardware). Seen as `S6CR = 0x451` after the first UART TX. The second `uart_tx_dma_start()` saw `EN` and refused, so `configASSERT` fired and the firmware reboot-looped | The TX TC handler clears `EN` itself. That is a no-op on silicon. Found by the BOOT frame's crash record (`ASSERT`, `telemetry.c:81`, task `tx`) on the first emulated boot |
| D15 | `STM32_Timer` raises the update event every `ARR` ticks (silicon: `ARR + 1`). TIM3 at 1 kHz (`ARR = 999`) gives a sample period of exactly **999 µs**, and 100 Hz gives 9999 µs. TIM2 is unaffected (G2) | The firmware keeps the silicon-correct `ARR = 10⁶/fs − 1`. The Robot suites expect `period − 1` through `${TIM_DT_OFFSET}` in `tests/robot/common.resource` |

## Bugs found by integration tests

- `plaus_check` treated `stuck_n = 0` as "flag every sample". It now means "check disabled", with a unit test (`test_stuck_disabled_when_n_is_zero`).
- The `SENSOR_FLAG_ON/OFF` event arg did not match the protocol (`(ch << 8) | flags`).
- GET_STATUS answered on the next 100 ms health slot, so a reply could take up to ~100 ms. The health task now waits on a notification with a deadline and answers within one TX slot. This also removed a duplicate HEALTH on every tenth cycle.
- The MSP high-water mark overflowed `u16`, because the MSP spanned all free SRAM. The MSP is now 2 KiB at the bottom of CCM, as PRD §15.2 specifies: overflow runs off `0x10000000` and faults instead of corrupting data.
- Apple clang 15 ASan aborts at startup on macOS 27, even on an empty `main`. The host tests now build with LLVM clang (`HOST_CC` in the Makefile).

## Decisions

- `RESET_COUNTERS` clears diagnostics only (HWMs, latency histogram, L2 max). Loss-accounting counters are never reset, so conservation holds over a whole run.
- The decoder's exact conservation check runs only after `SET_RATE 0` (stop and flush) followed by `GET_STATUS`. At any other point the PRD §18 in-flight bound is checked instead.
