"""Frame codec mirroring src/core/frame.c, msg.c and cmd.h (see docs/protocol.md).

decode_frame() returns a dict whose keys match the C struct field names; the C vector
generator (tests/vectors/gen_vectors.c) writes the same shape as JSON.
"""
import binascii
import struct

from . import cobs

FRAME_VERSION = 1
HDR = struct.Struct("<BBHIH")  # version, type, seq, t_us, payload_len
HDR_LEN, CRC_LEN, FRAME_MAX = 10, 2, 256
PAYLOAD_MAX = FRAME_MAX - HDR_LEN - CRC_LEN
WIRE_MAX = cobs.max_encoded(FRAME_MAX)  # longest valid wire frame, without the 0x00

TYPES = {
    0x01: "SAMPLE_BATCH", 0x02: "IMU_BATCH", 0x10: "BOOT", 0x11: "HEALTH", 0x12: "EVENT", 0x20: "ACK",
    0x80: "CMD_PING", 0x81: "CMD_GET_STATUS", 0x82: "CMD_SET_RATE", 0x83: "CMD_RESET_COUNTERS",
    0x84: "CMD_REBOOT", 0x90: "CMD_FI_ARM", 0x91: "CMD_FI_CLEAR",
}
TYPE_IDS = {v: k for k, v in TYPES.items()}
RESET_REASONS = ["POWER_ON", "CMD", "WDT_TASK_STALL", "HARDFAULT", "STACK_OVF", "ASSERT", "UNKNOWN_WDT"]
ACK_STATUS = ["OK", "ERR_UNKNOWN", "ERR_LEN", "ERR_RANGE", "UNSUPPORTED", "BUSY"]
FAULTS = {"STALL_PROC": 1, "TX_DELAY": 2, "HANG_TASK": 3, "UDF": 4, "STACK_OVF": 5, "ASSERT": 6,
          "DELAY_DMA_ISR": 7, "I2C_NACK": 8}
RATES_HZ = (0, 100, 250, 500, 1000, 2000, 4000, 8000)

# Rejection reasons in validation order; "type_len" = payload too short for its type.
REASONS = ("cobs", "short", "oversize", "crc", "version", "len", "type_len")


class FrameError(ValueError):
    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE; crc16(b"123456789") == 0x29B1."""
    return binascii.crc_hqx(data, 0xFFFF)


def encode_frame(type_: int, seq: int, t_us: int, payload: bytes = b"") -> bytes:
    """Complete wire frame including the trailing 0x00."""
    if len(payload) > PAYLOAD_MAX:
        raise ValueError(f"payload {len(payload)} B > {PAYLOAD_MAX}")
    raw = HDR.pack(FRAME_VERSION, type_, seq & 0xFFFF, t_us & 0xFFFFFFFF, len(payload)) + payload
    return cobs.encode(raw + struct.pack("<H", crc16(raw))) + b"\0"


def decode_frame(wire: bytes) -> dict:
    """wire excludes the delimiter. Raises FrameError(reason) in the same order as frame_decode()."""
    if len(wire) > WIRE_MAX:
        raise FrameError("oversize")
    try:
        raw = cobs.decode(wire)
    except ValueError:
        raise FrameError("cobs") from None
    if not raw:
        raise FrameError("cobs")  # C cobs_decode() reports an empty result as an error
    if len(raw) < HDR_LEN + CRC_LEN:
        raise FrameError("short")
    if len(raw) > FRAME_MAX:
        raise FrameError("oversize")
    body = raw[:-CRC_LEN]
    if crc16(body) != struct.unpack_from("<H", raw, len(body))[0]:
        raise FrameError("crc")
    version, type_, seq, t_us, plen = HDR.unpack_from(body)
    if version != FRAME_VERSION:
        raise FrameError("version")
    payload = body[HDR_LEN:]
    if plen != len(payload):
        raise FrameError("len")
    try:
        decoded = _DECODERS.get(type_, _raw)(payload)
    except struct.error:
        raise FrameError("type_len") from None
    return {"version": version, "type": type_, "name": TYPES.get(type_, f"0x{type_:02X}"), "seq": seq,
            "t_us": t_us, "payload_len": plen, "payload": decoded}


# ---- payload decoders: accept longer payloads (fields are only ever appended) ----

_SAMPLE = struct.Struct("<II4HH")
_IMU = struct.Struct("<II7h")
_BOOT = struct.Struct("<3B8s3BI8I16s24s4HBHB")
_HEALTH_A = struct.Struct("<13I3H2B4HB")
_HEALTH_B = struct.Struct("<H8I2I3IB")
_EVENT = struct.Struct("<IHHI")
_ACK = struct.Struct("<HBB")


def _need(p: bytes, n: int):
    if len(p) < n:
        raise struct.error("short payload")


def _count(p: bytes, max_count: int) -> int:
    _need(p, 1)
    if p[0] > max_count:
        raise struct.error("count out of range")
    return p[0]


def _str(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def _sample_batch(p):
    n = _count(p, 10)
    _need(p, 2 + n * _SAMPLE.size)
    samples = []
    for i in range(n):
        seq, t, r0, r1, r2, r3, status = _SAMPLE.unpack_from(p, 2 + i * _SAMPLE.size)
        samples.append({"seq": seq, "t_us": t, "raw": [r0, r1, r2, r3], "status": status})
    return {"count": n, "ch_mask": p[1], "samples": samples}


def _imu_batch(p):
    n = _count(p, 8)
    _need(p, 1 + n * _IMU.size)
    samples = []
    for i in range(n):
        v = _IMU.unpack_from(p, 1 + i * _IMU.size)
        samples.append({"seq": v[0], "t_us": v[1], "acc": list(v[2:5]), "gyr": list(v[5:8]), "temp": v[8]})
    return {"count": n, "samples": samples}


_BOOT_FIELDS = ("ver_major", "ver_minor", "ver_patch", "git_sha", "build_type", "fault_injection",
                "reset_reason", "boot_count", "detail", "pc", "lr", "psr", "cfsr", "hfsr", "mmfar", "bfar",
                "task", "file", "vrefint_cal", "ts_cal1", "ts_cal2", "sample_rate_hz", "batch",
                "ring_capacity", "pool_size")


def _boot(p):
    d = dict(zip(_BOOT_FIELDS, _BOOT.unpack_from(p)))
    for k in ("git_sha", "task", "file"):
        d[k] = _str(d[k])
    return d


_HEALTH_A_FIELDS = ("uptime_ms", "triggered", "delivered", "dropped_overrun", "dropped_ring", "dropped_txpool",
                    "frames_sent", "frames_dropped", "rx_frames_ok", "rx_frames_bad", "rx_bytes_dropped",
                    "event_drops", "tx_timeouts", "ring_occ", "ring_hwm", "ring_capacity", "txpool_free_min",
                    "txpool_size", "cpu_load_permille", "sample_rate_hz", "sensor_status", "fault_flags", "n_tasks")
HEALTH_MAX_TASKS = 8


def _health(p):
    d = dict(zip(_HEALTH_A_FIELDS, _HEALTH_A.unpack_from(p)))
    n = d["n_tasks"]
    if n > HEALTH_MAX_TASKS:
        raise struct.error("n_tasks out of range")
    off = _HEALTH_A.size
    d["stack_free_min_words"] = list(struct.unpack_from(f"<{n}H", p, off))
    v = _HEALTH_B.unpack_from(p, off + 2 * n)
    d["msp_free_bytes"] = v[0]
    d["l1_hist"] = list(v[1:9])
    for k, x in zip(("l1_max_us", "l2_max_us", "imu_samples", "imu_dropped", "i2c_errors", "imu_status"), v[9:]):
        d[k] = x
    return d


def _event(p):
    n = _count(p, 8)
    _need(p, 1 + n * _EVENT.size)
    events = []
    for i in range(n):
        t, id_, _rsv, arg = _EVENT.unpack_from(p, 1 + i * _EVENT.size)
        events.append({"t_us": t, "id": id_, "arg": arg})
    return {"count": n, "events": events}


def _ack(p):
    cmd_seq, cmd, status = _ACK.unpack_from(p)
    return {"cmd_seq": cmd_seq, "cmd": cmd, "status": status}


def _raw(p):
    return {"raw": p.hex()}


def _empty(p):
    return {}


_DECODERS = {
    0x01: _sample_batch, 0x02: _imu_batch, 0x10: _boot, 0x11: _health, 0x12: _event, 0x20: _ack,
    0x80: _empty, 0x81: _empty, 0x83: _empty, 0x84: _empty, 0x91: _empty,
    0x82: lambda p: {"rate_hz": struct.unpack_from("<H", p)[0]},
    0x90: lambda p: dict(zip(("fault_id", "fault_param"), struct.unpack_from("<BI", p))),
}

# ---- uplink commands ----

COMMANDS = {  # CLI name -> (type, payload struct format, argument names)
    "ping": (0x80, "", ()),
    "get-status": (0x81, "", ()),
    "set-rate": (0x82, "<H", ("hz",)),
    "reset-counters": (0x83, "", ()),
    "reboot": (0x84, "", ()),
    "fi-arm": (0x90, "<BI", ("fault_id", "param")),
    "fi-clear": (0x91, "", ()),
}


def encode_command(name: str, seq: int, *args: int, t_us: int = 0) -> bytes:
    """Wire frame for an uplink command. Arguments are not range-checked, so invalid values can
    be sent on purpose (fault-injection tests); the firmware answers with an error ACK."""
    type_, fmt, arg_names = COMMANDS[name]
    if len(args) != len(arg_names):
        raise ValueError(f"{name} takes {len(arg_names)} argument(s): {' '.join(arg_names) or '-'}")
    return encode_frame(type_, seq, t_us, struct.pack(fmt, *args) if fmt else b"")
