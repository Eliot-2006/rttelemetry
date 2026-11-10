# Telemetry wire protocol (v1)

Byte-level specification of the link between the firmware and the host. The C implementation is
`src/core/{cobs,crc16,frame,msg,cmd}.c`; the Python one is `tools/telemetry/`. Both are tested
against the same vectors (`tests/vectors/`), so this document, the C code and the Python code
describe one format. PRD §12 gives the rationale.

All multi-byte integers are **little-endian**. Structures are packed (no padding on the wire).

## 1. Framing

```
wire  := COBS(header || payload || crc16) || 0x00
header (10 B):
  off 0  u8  version       = 1
  off 1  u8  type          (§3, §4)
  off 2  u16 seq           frame counter, per direction, wraps at 2^16
  off 4  u32 t_us          TIM2 µs timestamp when the frame was finalized (wraps every 71.6 min)
  off 8  u16 payload_len
crc16 (2 B, LE) := CRC-16/CCITT-FALSE over header || payload
```

| Item | Value |
|---|---|
| CRC | poly 0x1021, init 0xFFFF, no reflection, xorout 0. Check: `"123456789"` → `0x29B1`. Python: `binascii.crc_hqx(data, 0xFFFF)` |
| COBS | Cheshire & Baker. Blocks of ≤ 254 data bytes; a block that ends after 254 bytes at the end of the input has no trailing code byte. Encoded size ≤ `n + n/254 + 1` (`COBS_MAX_ENCODED`) |
| `FRAME_MAX` | 256 B: header + payload + CRC before COBS |
| `FRAME_PAYLOAD_MAX` | 244 B |
| `FRAME_WIRE_MAX` | 259 B: COBS(256 B) + delimiter |
| Delimiter | a single `0x00` after every frame; never appears inside a frame |

### 1.1 Validation order

The decoder (`frame_decode()` in C, `decode_frame()` in Python) applies these checks in this
order. Each failure has its own counter (C `frame_err_t`, Python `FrameError.reason`).

| # | Check | C | Python |
|---|---|---|---|
| 1 | Bytes up to the next `0x00` form one candidate frame. Empty candidates (back-to-back delimiters) are skipped and not counted | `deframer_push()` | `stream.Deframer` |
| 2 | Wire length ≤ `COBS_MAX_ENCODED(FRAME_MAX)` (258). Longer input cannot decode to ≤ 256 B | `FRAME_ERR_OVERSIZE` | `oversize` |
| 3 | COBS decode succeeds: no `0x00` inside, no code byte that runs past the end, result non-empty and fits the scratch buffer | `FRAME_ERR_COBS` | `cobs` |
| 4 | Decoded length ≥ 12 (header + CRC) | `FRAME_ERR_SHORT` | `short` |
| 5 | Decoded length ≤ 256 | `FRAME_ERR_OVERSIZE` | `oversize` |
| 6 | CRC matches | `FRAME_ERR_CRC` | `crc` |
| 7 | `version == 1` | `FRAME_ERR_VERSION` | `version` |
| 8 | `payload_len == decoded length − 12` | `FRAME_ERR_LEN` | `len` |
| 9 | Type-specific payload length and count ranges (§3, §4) | `msg_get_*()` → false; `cmd_parse()` → `ACK_ERR_LEN`/`ACK_ERR_RANGE` | `type_len` |

The streaming deframer on the device buffers at most `FRAME_WIRE_MAX` bytes. A longer run of
non-zero bytes is discarded up to the next delimiter and counted in `deframer_t.overflows`; the
Python deframer counts it as `oversize`. In both, corruption costs only the frames whose bytes (or
bounding delimiters) were hit, and decoding resynchronizes at the next `0x00`.

To make step 5 distinguishable from step 3, pass a scratch buffer of `FRAME_SCRATCH_LEN`
(`FRAME_MAX + 1`) bytes. A smaller buffer reports an oversized frame as a COBS error.

### 1.2 Versioning rule

- `version` changes only for an incompatible change to the header, framing, or an existing field.
- Within one version, new fields are **only appended** to the end of a payload. Decoders accept
  a longer payload of the same version and ignore the tail. A payload shorter than its type needs is
  rejected (step 9).
- New message types and new event IDs can be added without a version change. Unknown types decode on
  the host as `{"raw": "<hex>"}`. The device answers unknown uplink types with `ACK_ERR_UNKNOWN`.
- **Uplink commands are length-exact** (`cmd_parse()` returns `ACK_ERR_LEN` on any other length), so the
  device never ignores an argument it does not understand. Adding an argument means a new command type.

## 2. Common enums

Reset reasons (`BOOT.reset_reason`): 0 POWER_ON, 1 CMD, 2 WDT_TASK_STALL, 3 HARDFAULT, 4 STACK_OVF,
5 ASSERT, 6 UNKNOWN_WDT.

ACK status: 0 OK, 1 ERR_UNKNOWN (unknown type), 2 ERR_LEN (payload length wrong), 3 ERR_RANGE
(argument out of range), 4 UNSUPPORTED (fault injection compiled out), 5 BUSY.

Fault IDs (`FI_ARM.fault_id`): 1 STALL_PROC, 2 TX_DELAY, 3 HANG_TASK, 4 UDF, 5 STACK_OVF, 6 ASSERT,
7 DELAY_DMA_ISR, 8 I2C_NACK.

Event IDs: `src/core/events.h` is the single source of truth (one `EVT_<NAME> = <id>, /* meaning */`
per line). The host reads names from that header (`tools/telemetry/events.py`).

Sample status word (`SAMPLE_BATCH` `status`, HEALTH `sensor_status`): 4 bits per channel, channel 0 in
bits 0–3 (`PLAUS_STATUS(ch, flags)`). Flags: bit 0 RANGE (raw < min or raw > max), bit 1 STUCK
(the N-th or later identical consecutive value), bit 2 ROC (|raw − previous| > threshold).

L1 histogram buckets (`HEALTH.l1_hist`): upper edges (inclusive) 500, 1000, 2000, 5000, 10000, 20000,
50000, 2³²−1 µs. Counts saturate at 2³²−1.

## 3. Downlink messages (device → host)

### 0x01 SAMPLE_BATCH (2 + 18·count B, count ≤ 10)

| Off | Type | Field |
|---|---|---|
| 0 | u8 | count |
| 1 | u8 | ch_mask (bit k = channel k enabled) |
| 2 + 18·i | | record i: |
| +0 | u32 | seq: sample sequence number, assigned at trigger, wraps at 2³² |
| +4 | u32 | t_us: trigger timestamp |
| +8 | u16[4] | raw: ADC counts, channels 0..3 |
| +16 | u16 | status: plausibility flags (§2) |

### 0x02 IMU_BATCH (1 + 22·count B, count ≤ 8)

| Off | Type | Field |
|---|---|---|
| 0 | u8 | count |
| 1 + 22·i | | record i: `u32 seq` +0, `u32 t_us` +4, `i16 acc[3]` +8, `i16 gyr[3]` +14, `i16 temp` +20 |

### 0x10 BOOT (102 B)

| Off | Type | Field | Off | Type | Field |
|---|---|---|---|---|---|
| 0 | u8 | ver_major | 34 | u32 | cfsr |
| 1 | u8 | ver_minor | 38 | u32 | hfsr |
| 2 | u8 | ver_patch | 42 | u32 | mmfar |
| 3 | char[8] | git_sha (not NUL-terminated) | 46 | u32 | bfar |
| 11 | u8 | build_type (0 debug, 1 release) | 50 | char[16] | task (NUL-padded) |
| 12 | u8 | fault_injection (1 = FI compiled in) | 66 | char[24] | file (NUL-padded) |
| 13 | u8 | reset_reason (§2) | 90 | u16 | vrefint_cal |
| 14 | u32 | boot_count | 92 | u16 | ts_cal1 |
| 18 | u32 | detail (missing mask or line number) | 94 | u16 | ts_cal2 |
| 22 | u32 | pc | 96 | u16 | sample_rate_hz |
| 26 | u32 | lr | 98 | u8 | batch |
| 30 | u32 | psr | 99 | u16 | ring_capacity |
| | | | 101 | u8 | pool_size |

The crash fields (`detail`..`file`) are zero unless the previous reset recorded them. The host
converts raw ADC counts with the calibration constants and falls back to datasheet values when a
constant is 0 or 0xFFFF (PRD D7).

### 0x11 HEALTH (124 + 2·n_tasks B, n_tasks ≤ 8)

| Off | Type | Field |
|---|---|---|
| 0 | u32 ×13 | uptime_ms, triggered, delivered, dropped_overrun, dropped_ring, dropped_txpool, frames_sent, frames_dropped, rx_frames_ok, rx_frames_bad, rx_bytes_dropped, event_drops, tx_timeouts (in this order, 4 B each) |
| 52 | u16 | ring_occ |
| 54 | u16 | ring_hwm |
| 56 | u16 | ring_capacity |
| 58 | u8 | txpool_free_min |
| 59 | u8 | txpool_size |
| 60 | u16 | cpu_load_permille |
| 62 | u16 | sample_rate_hz |
| 64 | u16 | sensor_status |
| 66 | u16 | fault_flags |
| 68 | u8 | n_tasks |
| 69 | u16 ×n_tasks | stack_free_min_words |
| o = 69 + 2·n_tasks | u16 | msp_free_bytes |
| o+2 | u32 ×8 | l1_hist |
| o+34 | u32 | l1_max_us |
| o+38 | u32 | l2_max_us |
| o+42 | u32 | imu_samples |
| o+46 | u32 | imu_dropped |
| o+50 | u32 | i2c_errors |
| o+54 | u8 | imu_status |

`delivered` counts samples placed into frames handed to the TX task (device side). The host counts
what it actually received. Conservation (PRD §18): `triggered = delivered + dropped_overrun +
dropped_ring + dropped_txpool + in_flight`, with `0 ≤ in_flight ≤ ring_capacity + batch + pool·batch`.
After `SET_RATE 0`, `in_flight = 0` (`tools/telemetry/accounting.py`).

### 0x12 EVENT (1 + 12·count B, count ≤ 8)

| Off | Type | Field |
|---|---|---|
| 0 | u8 | count |
| 1 + 12·i | | record i: `u32 t_us` +0, `u16 id` +4 (`events.h`), `u16 reserved = 0` +6 (ignored by decoders), `u32 arg` +8 |

### 0x20 ACK (4 B)

| Off | Type | Field |
|---|---|---|
| 0 | u16 | cmd_seq: `seq` of the command frame being answered |
| 2 | u8 | cmd: type of that command |
| 3 | u8 | status (§2) |

## 4. Uplink commands (host → device)

Same framing. `t_us` is unused (the host sends 0). `cmd_parse()` checks, in order: fault-injection
availability (FI commands only) → exact length → argument range.

| Type | Command | Payload | Range | Notes |
|---|---|---|---|---|
| 0x80 | PING | — | | ACK |
| 0x81 | GET_STATUS | — | | ACK + immediate HEALTH |
| 0x82 | SET_RATE | u16 hz | {0, 100, 250, 500, 1000, 2000, 4000, 8000} | 0 = stop + flush |
| 0x83 | RESET_COUNTERS | — | | |
| 0x84 | REBOOT | — | | reset reason CMD |
| 0x90 | FI_ARM | u8 fault_id, u32 param | fault_id 1..8 | `ACK_UNSUPPORTED` when fault injection is compiled out |
| 0x91 | FI_CLEAR | — | | `ACK_UNSUPPORTED` when fault injection is compiled out |

Any other type returns `ACK_ERR_UNKNOWN`. Host CLI: `python -m telemetry encode-cmd <name> [args] --seq N --out F`
(names: `ping`, `get-status`, `set-rate HZ`, `reset-counters`, `reboot`, `fi-arm ID PARAM`, `fi-clear`).

## 5. Test vectors

- `tests/vectors/out/<type>.bin` + `.json`: one wire frame per downlink type and its expected
  decoding, written by `tests/vectors/gen_vectors.c` (`build/host/tests/gen_vectors tests/vectors/out`
  regenerates them; ctest `xv_vectors_up_to_date` fails if the committed files are stale).
  `tools/test_vectors.py` decodes them and re-encodes them bit-exactly.
- `tests/vectors/cmd_*.bin`: command frames encoded by the Python tool (`cd tools && python test_vectors.py`
  regenerates them), decoded in C by `tests/unit/test_cmd_vectors.c`.

## 6. Worked examples

Generated from the vectors in §5. `header`, `payload` and `crc` show the frame before COBS; `wire` shows
the bytes on the link.

### SAMPLE_BATCH (`tests/vectors/out/sample_batch.*`)

seq 0xFFFF, t_us 1704, 10 samples with sample `seq` 4294967290..4294967299 (crosses the u32 wrap), ch_mask 0x0F, sample 3 has status 0x0021 (ch0 RANGE, ch1 STUCK). 196 bytes on the wire, matching the PRD §12.4 `[CALC]` figure.

```
header   01 01 ff ff a8 06 00 00 b6 00
payload  0a 0f fa ff ff ff 80 e3 ff ff 00 00 ff 0f ac 03
         dc 05 00 00 fb ff ff ff 68 e7 ff ff 99 01 fe 0f
         ad 03 dc 05 00 00 fc ff ff ff 50 eb ff ff 32 03
         fd 0f ae 03 dc 05 00 00 fd ff ff ff 38 ef ff ff
         cb 04 fc 0f af 03 dc 05 21 00 fe ff ff ff 20 f3
         ff ff 64 06 fb 0f b0 03 dc 05 00 00 ff ff ff ff
         08 f7 ff ff fd 07 fa 0f b1 03 dc 05 00 00 00 00
         00 00 f0 fa ff ff 96 09 f9 0f b2 03 dc 05 00 00
         01 00 00 00 d8 fe ff ff 2f 0b f8 0f b3 03 dc 05
         00 00 02 00 00 00 c0 02 00 00 c8 0c f7 0f b4 03
         dc 05 00 00 03 00 00 00 a8 06 00 00 61 0e f6 0f
         b5 03 dc 05 00 00
crc      67 fe
wire (196 B)
07 01 01 ff ff a8 06 01 02 b6 0b 0a 0f fa ff ff
ff 80 e3 ff ff 01 07 ff 0f ac 03 dc 05 01 11 fb
ff ff ff 68 e7 ff ff 99 01 fe 0f ad 03 dc 05 01
11 fc ff ff ff 50 eb ff ff 32 03 fd 0f ae 03 dc
05 01 12 fd ff ff ff 38 ef ff ff cb 04 fc 0f af
03 dc 05 21 11 fe ff ff ff 20 f3 ff ff 64 06 fb
0f b0 03 dc 05 01 11 ff ff ff ff 08 f7 ff ff fd
07 fa 0f b1 03 dc 05 01 01 01 01 01 0d f0 fa ff
ff 96 09 f9 0f b2 03 dc 05 01 02 01 01 01 0d d8
fe ff ff 2f 0b f8 0f b3 03 dc 05 01 02 02 01 01
03 c0 02 01 09 c8 0c f7 0f b4 03 dc 05 01 02 03
01 01 03 a8 06 01 09 61 0e f6 0f b5 03 dc 05 01
03 67 fe 00
```

### IMU_BATCH (`imu_batch.*`)

seq 2, t_us 5040000, 8 records.

```
header   01 02 02 00 80 e7 4c 00 b1 00
payload  08 64 00 00 00 40 4b 4c 00 00 80 00 00 00 20 ff
         7f 00 00 00 00 50 fb 65 00 00 00 08 5e 4c 00 01
         80 e8 03 00 20 fe 7f f9 ff 00 00 51 fb 66 00 00
         00 d0 70 4c 00 02 80 d0 07 00 20 fd 7f f2 ff 00
         00 52 fb 67 00 00 00 98 83 4c 00 03 80 b8 0b 00
         20 fc 7f eb ff 00 00 53 fb 68 00 00 00 60 96 4c
         00 04 80 a0 0f 00 20 fb 7f e4 ff 00 00 54 fb 69
         00 00 00 28 a9 4c 00 05 80 88 13 00 20 fa 7f dd
         ff 00 00 55 fb 6a 00 00 00 f0 bb 4c 00 06 80 70
         17 00 20 f9 7f d6 ff 00 00 56 fb 6b 00 00 00 b8
         ce 4c 00 07 80 58 1b 00 20 f8 7f cf ff 00 00 57
         fb
crc      da 3b
wire (191 B)
04 01 02 02 04 80 e7 4c 02 b1 03 08 64 01 01 04
40 4b 4c 01 02 80 01 01 04 20 ff 7f 01 01 01 04
50 fb 65 01 01 04 08 5e 4c 05 01 80 e8 03 06 20
fe 7f f9 ff 01 04 51 fb 66 01 01 04 d0 70 4c 05
02 80 d0 07 06 20 fd 7f f2 ff 01 04 52 fb 67 01
01 04 98 83 4c 05 03 80 b8 0b 06 20 fc 7f eb ff
01 04 53 fb 68 01 01 04 60 96 4c 05 04 80 a0 0f
06 20 fb 7f e4 ff 01 04 54 fb 69 01 01 04 28 a9
4c 05 05 80 88 13 06 20 fa 7f dd ff 01 04 55 fb
6a 01 01 04 f0 bb 4c 05 06 80 70 17 06 20 f9 7f
d6 ff 01 04 56 fb 6b 01 01 04 b8 ce 4c 05 07 80
58 1b 06 20 f8 7f cf ff 01 05 57 fb da 3b 00
```

### BOOT (`boot.*`)

Reset after a HardFault (reason 3) in task `cmd`, fault-injection debug build, git `0123abcd`.

```
header   01 10 00 00 d2 04 00 00 66 00
payload  00 01 00 30 31 32 33 61 62 63 64 00 01 03 03 00
         00 00 00 00 00 00 a4 12 00 08 11 0f 00 08 00 00
         00 01 00 00 01 00 00 00 00 40 34 ed 00 e0 38 ed
         00 e0 63 6d 64 00 00 00 00 00 00 00 00 00 00 00
         00 00 66 61 75 6c 74 5f 69 6e 6a 65 63 74 2e 63
         00 00 00 00 00 00 00 00 00 00 77 06 ad 03 b6 04
         e8 03 0a 80 00 04
crc      ea 01
wire (116 B)
03 01 10 01 03 d2 04 01 02 66 01 02 01 09 30 31
32 33 61 62 63 64 04 01 03 03 01 01 01 01 01 01
03 a4 12 04 08 11 0f 02 08 01 01 02 01 01 02 01
01 01 01 04 40 34 ed 04 e0 38 ed 05 e0 63 6d 64
01 01 01 01 01 01 01 01 01 01 01 01 0f 66 61 75
6c 74 5f 69 6e 6a 65 63 74 2e 63 01 01 01 01 01
01 01 01 01 0b 77 06 ad 03 b6 04 e8 03 0a 80 04
04 ea 01 00
```

### HEALTH (`health.*`)

n_tasks 5, so the payload is 124 + 2·5 = 134 bytes.

```
header   01 11 07 00 bb 4b 4c 00 86 00
payload  88 13 00 00 88 13 00 00 60 13 00 00 01 00 00 00
         02 00 00 00 0a 00 00 00 f7 01 00 00 01 00 00 00
         04 00 00 00 02 00 00 00 00 00 00 00 00 00 00 00
         00 00 00 00 03 00 0e 00 80 00 02 04 29 00 e8 03
         00 04 00 00 05 c9 00 96 00 04 01 0e 01 5a 00 2c
         03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
         00 e0 01 00 00 10 00 00 00 00 00 00 00 00 00 00
         00 c6 2a 00 00 25 00 00 00 10 04 00 00 00 00 00
         00 01 00 00 00 01
crc      da 1f
wire (148 B)
04 01 11 07 04 bb 4b 4c 02 86 03 88 13 01 03 88
13 01 03 60 13 01 02 01 01 01 02 02 01 01 02 0a
01 01 03 f7 01 01 02 01 01 01 02 04 01 01 02 02
01 01 01 01 01 01 01 01 01 01 01 01 01 01 02 03
02 0e 02 80 04 02 04 29 03 e8 03 02 04 01 03 05
c9 02 96 06 04 01 0e 01 5a 03 2c 03 01 01 01 01
01 01 01 01 01 01 01 01 01 01 01 03 e0 01 01 02
10 01 01 01 01 01 01 01 01 01 01 03 c6 2a 01 02
25 01 01 03 10 04 01 01 01 01 01 02 01 01 01 04
01 da 1f 00
```

### EVENT (`event.*`)

3 records: BOOT (arg 3), RESET_REASON (arg 3 = HARDFAULT), RING_OVF_ON at t_us 0xFFFFFFFF.

```
header   01 12 08 00 ff ff ff ff 25 00
payload  03 0a 00 00 00 01 00 00 00 03 00 00 00 14 00 00
         00 02 00 00 00 03 00 00 00 ff ff ff ff 03 00 00
         00 ff ff ff ff
crc      60 08
wire (51 B)
04 01 12 08 06 ff ff ff ff 25 03 03 0a 01 01 02
01 01 01 02 03 01 01 02 14 01 01 02 02 01 01 02
03 01 01 06 ff ff ff ff 03 01 01 07 ff ff ff ff
60 08 00
```

### ACK (`ack.*`)

Reply to command frame seq 3, `SET_RATE` (0x82), status ERR_RANGE (3).

```
header   01 20 09 00 09 03 00 00 04 00
payload  03 00 82 03
crc      10 3f
wire (18 B)
04 01 20 09 03 09 03 01 02 04 02 03 05 82 03 10
3f 00
```

### Uplink SET_RATE 1000 (`tests/vectors/cmd_set_rate_1000.bin`)

Command frame seq 3, t_us 0.

```
header   01 82 03 00 00 00 00 00 02 00
payload  e8 03
crc      4a 5b
wire (16 B)
04 01 82 03 01 01 01 01 02 02 05 e8 03 4a 5b 00
```

### Uplink FI_ARM STALL_PROC 500 (`cmd_fi_arm.bin`)

Command frame seq 7: fault_id 1, param 500.

```
header   01 90 07 00 00 00 00 00 05 00
payload  01 f4 01 00 00
crc      c4 0a
wire (19 B)
04 01 90 07 01 01 01 01 02 05 04 01 f4 01 01 03
c4 0a 00
```
