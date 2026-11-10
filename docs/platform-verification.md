# Platform verification (PRD M0)

This file records how the emulated platform was verified before the telemetry firmware was
built on it. The evidence is produced by `tests/platform/gates.c`, a bare-metal firmware that
uses the production drivers, and checked by `tests/platform/gates.robot`. Run it with:

```sh
make bootstrap        # once: pinned toolchain + Renode into ~/.cache/rttelemetry-tools
make test-platform    # builds build/fw-debug/src/platform_gates.elf and runs the gates in Renode
```

All results below are `[EMU]`. They come from Renode virtual time, which is not cycle-accurate.

## Pinned versions (2026-11-09)

| Component | Version |
|---|---|
| Arm GNU Toolchain | 15.2.Rel1 (`arm-none-eabi-gcc 15.2.1 20251203`), SHA-256 pinned in `scripts/bootstrap.sh` |
| Renode | 1.17.0 (portable: `osx-arm64` dmg, `linux` / `linux-arm64` tarballs), SHA-256 pinned |
| FreeRTOS-Kernel | V11.3.3 (submodule) |
| CMSIS-Core | CMSIS_6 v6.4.0 (submodule, Core headers only) |
| ST device files | cmsis-device-f4 v2.6.11 (submodule) |
| Unity | v2.7.0 (submodule) |
| Robot Framework | 6.1 (matches Renode 1.17.0 `tests/requirements.txt`) |

## Gate results

| Gate | Check | Result | Evidence |
|---|---|---|---|
| G1 | Reset → `main`; HSE/PLL 168 MHz config completes | **PASS** | `G1 PASS sysclk=168000000 rcc_csr=0x0E000000` |
| G2 | TIM2 at 1 MHz over 1 s virtual | **PASS** | delta = **1 000 000** µs (overlay frequency override works) |
| G3 | FreeRTOS tick rate | Verified in M2 with the RTOS firmware (`IT-HEALTH-1`) | — |
| G4 | BASEPRI masking: prio-4 IRQ runs inside a kernel-style critical section, prio-6 deferred | **PASS** | `G4 PASS hi_in_crit=1 lo_in_crit=0 lo_after=1` |
| G5 | USART2 TX via DMA1_Stream6, TC IRQ exactly once | **PASS** | 200-byte pattern captured intact; `tc=1` |
| G6 | ADC1 overlay: SW-triggered 4-channel scan → DMA2_Stream0 → TC IRQ, data from feed files | **PASS** | 1000 scans; first `0,4095,1000,1500`; sums `499500,3595500,1002997,1500000` match the feed files exactly; `us_per_scan=400` |
| G7 | LSM6DSO on I2C1: WHO_AM_I and accelerometer read-back | **PASS (with fallback)** | `who=0x6C`; fed (0.5, −0.25, 1.0) g read back as `8192, -4096, 16384` LSB (N=1, N=2 and N>2 receive paths). DRDY interrupt **not modeled** in Renode 1.17.0, see D11 |
| G8 | `.noinit` survives an IWDG-triggered reset | **PASS** | Watchdog reset fires at +49 ms virtual; magic word read back after reboot |
| G9 | `UDF #0` → HardFault with correct stacked PC | **PASS** | `pc == gate_udf_site`, `CFSR=0x00010000` (UNDEFINSTR) |
| G10 | USART2 RXNE interrupt from Robot `Write Char On Uart` | **PASS** | `rx=0x58` |
| G11 | Headless run in CI on Linux and locally on macOS arm64 | macOS arm64: **PASS** (this run). Linux: verified by the CI `renode` job | — |

## Final Renode overlay

`renode/telemetry_board.repl` (see the file for comments):
- `nvic.systickFrequency = 168 MHz` (stock model: 72 MHz).
- `timer2/timer3.frequency = 84 MHz` (stock model: fixed 10 MHz). This keeps prescaler math identical to silicon.
- `ccm`: 64 KiB at `0x10000000` (the stock model doesn't map it).
- `adc1`: `Analog.STM32_ADC` at `0x40012000`, IRQ 18, `DMARequest → dma2@0`.
- `imu`: `Sensors.LSM6DSO_IMU` on `i2c1` at `0x6A`.

`renode/telemetry.resc` defines the `reset` macro (reloads `$elf`). Without it, Renode logs
`No action for reset` and a watchdog/software reset does nothing. RAM is not cleared by a reset
(`MappedMemory.Reset()` is a no-op), so the `.noinit` reset record works.

## Divergences found during M0 (added to the register in `docs/design.md`)

| ID | Divergence (Renode 1.17.0) | Consequence / handling |
|---|---|---|
| D10 | `STM32_ADC` only implements 32-bit register access; 16-bit DMA reads of `ADC_DR` return 0 | ADC DMA uses 32-bit peripheral **and** memory width (valid on silicon: upper half of DR is 0 in independent mode) |
| D11 | `LSM6DSO_IMU` in 1.17.0 has `INT1_DRDY_XL/G` as unimplemented tags (added on Renode master later); `STATUS_REG` unhandled | IMU is sampled by `imu_task` on a fixed period equal to the ODR (single binary on both targets) rather than by DRDY interrupt |
| D12 | `STM32F1_I2C` model ignores `ACK`, `POS`, `CCR`, `TRISE`, `SWRST`; a single-byte (N=1) receive followed by another N=1 receive can leave the model in its `LastRead` state | Production code never issues back-to-back N=1 reads (IMU uses a 14-byte burst; probes use N=2). The silicon-correct AN2824 sequences are kept |
| D13 | `STM32_ADC` conversion time is fixed at 100 µs per channel: a 4-channel scan takes **400 µs** of virtual time | Emulated scans saturate at ~2.5 kHz. At 4 kHz and 8 kHz the firmware's overrun path (`dropped_overrun`) is exercised deterministically. Silicon scan time is ~33 µs `[CALC]` |
| — | `USART_CR3.DMAT` is a tag (ignored), but memory→peripheral DMA completes on stream enable, so TX via DMA works functionally (instantaneous) | Covered by D1 |
| — | `RCC_CSR` reads `0x0E000000` (POR/PIN/BOR) after every reset | Covered by D6 |

## Decisions recorded

- **Shared tool cache.** `scripts/bootstrap.sh` installs into `~/.cache/rttelemetry-tools` (override with `TOOLS_DIR`) instead of `.tools/`. Every git worktree reuses one ~300 MB install.
- **Gate firmware kept permanently** as a platform regression suite (`tests/platform/`), instead of a throwaway `spikes/m0/`. It catches Renode upgrades that change model behavior.
- **No `--specs=nosys.specs`.** The firmware never calls newlib I/O, and `_sbrk` is a trapping stub (no heap).
