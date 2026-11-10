"""CLI.

  python -m telemetry decode (--file F | --tcp HOST:PORT | --serial DEV [--baud N]) [--jsonl OUT] [--summary]
  python -m telemetry encode-cmd <name> [args...] [--seq N] --out F

Run from tools/ (or with tools/ on PYTHONPATH). decode writes one JSON object per frame to
--jsonl (default stdout unless only --summary is given); EVENT records get an extra "name".
"""
import argparse
import json
import socket
import sys
from collections import Counter

from . import events
from .accounting import Accounting
from .frames import COMMANDS, encode_command
from .stream import Deframer


def _chunks(args):
    if args.file:
        with open(args.file, "rb") as f:
            while chunk := f.read(65536):
                yield chunk
    elif args.tcp:
        host, port = args.tcp.rsplit(":", 1)
        with socket.create_connection((host, int(port))) as s:
            while chunk := s.recv(65536):
                yield chunk
    else:
        import serial  # pyserial: only needed for the hardware link

        with serial.Serial(args.serial, args.baud, timeout=0.1) as s:
            while True:
                yield s.read(4096)


def _decode(args) -> int:
    out = None
    if args.jsonl == "-" or (args.jsonl is None and not args.summary):
        out = sys.stdout
    elif args.jsonl:
        out = open(args.jsonl, "w")
    d, acct, by_type = Deframer(), Accounting(args.batch, args.pool), Counter()
    try:
        for chunk in _chunks(args):
            for f in d.feed(chunk):
                by_type[f["name"]] += 1
                acct.on_frame(f)
                if f["name"] == "EVENT":
                    for e in f["payload"]["events"]:
                        e["name"] = events.name(e["id"])
                if out:
                    out.write(json.dumps(f) + "\n")
    except KeyboardInterrupt:
        pass
    finally:
        if out and out is not sys.stdout:
            out.close()
    if args.summary:
        summary = {
            "frames_ok": d.frames_ok, "frames_by_type": dict(by_type), "rejects": dict(d.rejects),
            "samples_received": acct.received, "missing_seq": acct.missing,
            "bound_violations": len(acct.violations),
            "accounting": acct.check_exact() if acct.last_health else None,
        }
        print(json.dumps(summary, indent=2), file=sys.stderr if out is sys.stdout else sys.stdout)
    return 0


def _encode_cmd(args) -> int:
    wire = encode_command(args.name, args.seq, *args.args)
    if args.out == "-":
        sys.stdout.buffer.write(wire)
    else:
        with open(args.out, "wb") as f:
            f.write(wire)
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="python -m telemetry")
    sub = p.add_subparsers(dest="cmd", required=True)

    dec = sub.add_parser("decode", help="decode a telemetry byte stream")
    src = dec.add_mutually_exclusive_group(required=True)
    src.add_argument("--file")
    src.add_argument("--tcp", metavar="HOST:PORT")
    src.add_argument("--serial", metavar="DEV")
    dec.add_argument("--baud", type=int, default=921600)
    dec.add_argument("--jsonl", metavar="OUT", help="'-' for stdout")
    dec.add_argument("--summary", action="store_true", help="print counts and the accounting check at the end")
    dec.add_argument("--batch", type=int, default=10, help="samples per batch (accounting bound)")
    dec.add_argument("--pool", type=int, default=4, help="frame pool size (accounting bound)")
    dec.set_defaults(func=_decode)

    enc = sub.add_parser("encode-cmd", help="write one uplink command frame")
    enc.add_argument("name", choices=sorted(COMMANDS))
    enc.add_argument("args", nargs="*", type=lambda s: int(s, 0))
    enc.add_argument("--seq", type=lambda s: int(s, 0), default=0)
    enc.add_argument("--out", required=True, help="'-' for stdout")
    enc.set_defaults(func=_encode_cmd)

    args = p.parse_args(argv)
    try:
        return args.func(args)
    except ValueError as e:
        p.error(str(e))


if __name__ == "__main__":
    sys.exit(main())
