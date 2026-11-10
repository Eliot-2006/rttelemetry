# Hardware

**Status: no hardware has been used.** The firmware is written against the STM32F407 reference
manual (RM0090) and the STM32F4DISCOVERY user manual (UM1472), and is verified only in Renode 1.17.0.
Everything on this page about the board is a plan, not a validated setup. The hardware milestone
(PRD M9) has not been done.

## Target and kit (PRD §5.3, approximate 2026 prices)

| Item | Purpose | Approx. |
|---|---|---|
| STM32F407G-DISC1 | Target; on-board ST-LINK for flash/debug | US$25–35 |
| 3.3 V USB-UART adapter (CP2102N or FT232R, ≥ 921600 baud) | Telemetry link | US$8–15 |
| SparkFun LSM6DSO breakout | IMU (same part as the Renode model) | US$12–15 |
| 10 kΩ potentiometer, resistors, jumpers, breadboard | ADC inputs | US$5–10 |
| 8-channel 24 MHz USB logic analyzer (sigrok/PulseView) | Independent latency/jitter measurement | US$12–20 |

## Pin plan (as implemented in `src/drivers/`)

| Function | Peripheral | Pin | Notes |
|---|---|---|---|
| Telemetry TX / RX | USART2 AF7 | PA2 / PA3 | 921600 baud. BRR from 42 MHz gives −0.93 % `[CALC]`. RX has a pull-up |
| ADC ch0 | ADC1_IN1 | PA1 | Potentiometer |
| ADC ch1 | ADC1_IN8 | PB0 | Resistor divider |
| ADC ch2 / ch3 | ADC1_IN16 / IN17 | internal | Temperature sensor / VREFINT (long sampling time set) |
| IMU | I2C1 AF4, open drain, pull-ups | PB8 SCL / PB9 SDA | 0x6A. I2C1 is shared with the on-board CS43L22 codec (0x4A) |
| Timing markers | GPIO out | PD12 ISR, PD13 proc, PD14 TX DMA, PD15 fault | On-board LEDs; logic-analyzer probe points |

## Flashing (planned, not yet performed)

```sh
make fw-release
openocd -f board/stm32f4discovery.cfg -c "program build/fw-release/src/telemetry.elf verify reset exit"
cd tools && python -m telemetry decode --serial /dev/cu.usbserial-XXXX --baud 921600 --summary
```

## Remaining hardware-dependent work

| Item | PRD | Why it needs hardware |
|---|---|---|
| Flash the CI release ELF and check its SHA-256 | M9 | Board |
| HIL-1..4 runner (`tests/hil/`, serial transport for `TelemetryLibrary`) | §19.5 | Not built: nothing to run it against |
| B-01 rate sweep on silicon: real zero-loss rate (expected link-bound ≈ 4 kHz `[CALC]`) | P-01 | Renode's ADC scan time differs (D13) |
| B-02 end-to-end latency by logic analyzer; L2 percentiles | P-03, P-04 | Needs board + analyzer |
| B-03 ring vs FreeRTOS queue, DWT cycles | §9.6 | Renode is not cycle-accurate (D8) |
| B-05 real 115200-baud link | — | Renode UART has no baud timing (D1) |
| B-06 sampling jitter histogram (P-05 ≤ 5 µs), P-06 DMA ISR ≤ 3 µs, P-07 CPU ≤ 10 % | P-05..P-07 | Timing |
| B-07 24 h soak | P-10 | Board |
| IMU on the DRDY interrupt (PRD design) instead of polling | §10.3 | D11 forced polling; EXTI wiring untested |
| I²C bus-recovery and unplug test (FI-12 by unplugging) | FI-12 | Physical bus |
| Bus fault from a wild pointer; DMA into CCM failing on silicon | D4, D5 | Renode does not fault |
| Confirm the CS43L22 codec stays off the bus (firmware holds PD4 low) | §5.4 | Only matters on the Discovery board |
| Renode-vs-silicon comparison section (explain every delta > 10 %) | M9 | Needs the measurements above |
| Factory calibration values in BOOT (Renode reads 0, D7) | D7 | Board |
