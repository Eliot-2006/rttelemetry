# rttelemetry: FreeRTOS sensor telemetry for the STM32F407

C11 firmware that samples four ADC channels and an LSM6DSO IMU and sends framed, CRC-checked
telemetry over UART. It runs on FreeRTOS on an STM32F407, entirely from static memory. The
pipeline is TIM3 trigger → ADC scan via DMA → lock-free SPSC ring → processing task → frame
pool → DMA UART. Loss is accounted exactly at every layer: every sample is either delivered or
counted as dropped at the layer that dropped it. A window watchdog supervisor, crash records that
survive reset, and an uplink command channel with fault injection make every failure path
testable. The same ELF is built for silicon and runs unmodified in the Renode emulator, where the
whole test and benchmark suite runs in CI.

> **Validation status.** Everything here is verified in **Renode 1.17.0 emulation** `[EMU]` and in
> host builds `[HOST]`. **No physical hardware has been used.** Emulated timing is deterministic
> but not cycle-accurate. The hardware work that remains is listed in [docs/hardware.md](docs/hardware.md).

## Headline results

| Metric | Result | Bench |
|---|---|---|
| Max zero-loss rate, 4 ADC channels | 2000 Hz `[EMU]`. 4 and 8 kHz overrun in Renode by construction (its ADC takes 400 µs per scan, D13); every lost sample is counted | B-01 |
| Sample-to-frame latency (L1) at 1 kHz | p50 5.41 ms, p99 9.41 ms, max 9.72 ms `[EMU]` (target p99 ≤ 10 ms) | B-02 |
| 30 min soak at 1 kHz | 1 801 802 samples, 0 resets, 0 CRC/COBS rejects, 0 drops, exact conservation `[EMU]` | B-07 |
| Slow link (emulated 115200 baud) | Drops only at the frame pool, 0 at the ring; conservation exact `[EMU]` | B-05 |
| Corrupted-frame detection | 100 % of 100 000 frames with 1–3 bit flips or ≤ 16-bit bursts `[HOST]` | B-09 |
| Worst task stack use at 1 kHz | 35.7 % (`tx`); heap 0 bytes `[EMU]`/`[BUILD]` | B-04 |

Every value comes from [docs/results.md](docs/results.md), which `bench/report.py` generates from
`bench/results/` (all at commit `f0f20ead`). `[EMU]` timings are Renode virtual time, not silicon.

## Quickstart

```sh
git clone --recursive <repo> && cd rttelemetry
make bootstrap   # pinned Arm GNU 15.2, Renode 1.17.0, Python venv -> ~/.cache/rttelemetry-tools
make demo        # ~35 s: live dashboard, faults injected, watchdog reset, conservation check
make test        # host unit/TSan/Python tests
make test-renode # platform gates + 23 Renode integration and fault-injection tests
```

Requires macOS arm64 or Linux x86_64/arm64, CMake ≥ 3.25, Ninja and Python 3.12. On macOS 27 the
host tests use Homebrew LLVM (`brew install llvm`), because Apple clang 15's ASan aborts at startup.
`make ci-docker` runs the Linux CI path in `ubuntu:24.04`.

## Architecture

```
TIM3 ISR ─SWSTART─> ADC1 scan ─DMA─> DMA ISR ─ring_push─> [SPSC ring] ─> proc_task ─┐
                                                                (flags, batch 10)    │
imu_task (5 ms poll, I2C1) ─IMU_BATCH──────────────────────────────────────────────>├─> frame pool ─> tx_task ─DMA─> USART2
health_task (BOOT, HEALTH 1 Hz, events, watchdog) ─────────────────────────────────>│
USART2 RX ISR ─> [rx ring] ─> cmd_task (PING, GET_STATUS, SET_RATE, REBOOT, FI_ARM) ┘
```

| Topic | Where |
|---|---|
| Architecture, invariants, fault handling, Renode divergence register (D1–D15) | [docs/design.md](docs/design.md) |
| Wire protocol: COBS + CRC-16, message layouts, worked examples | [docs/protocol.md](docs/protocol.md) |
| Test layers and the integration / fault-injection catalog | [docs/testing.md](docs/testing.md) |
| Benchmarks | [docs/results.md](docs/results.md) |
| Response-time analysis tool | [docs/scheduling.md](docs/scheduling.md) |
| Board, pins, remaining hardware work | [docs/hardware.md](docs/hardware.md) |
| Requirements | [docs/PRD.md](docs/PRD.md) |
| Progress log, bugs found by tests, decisions | [docs/progress.md](docs/progress.md) |

## Repository layout

```
src/core/      portable C11 (no RTOS, no hardware): ring, crc16, cobs, frame, msg, cmd, plaus, hist
src/app/       FreeRTOS tasks and ISRs          src/drivers/  register-level CMSIS drivers (no HAL)
src/platform/  linker script, FreeRTOSConfig.h, fault capture
tests/         unit, fuzz, TSan stress, C/Python vectors, Renode platform gates and Robot suites
tools/         Python decoder/CLI (`python -m telemetry`), demo, RTA, feed generator, size budgets
bench/         benchmark runner and report generator
renode/        board overlay, machine script, ADC feed files
```
