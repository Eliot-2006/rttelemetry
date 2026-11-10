"""Event-ID names, parsed from src/core/events.h (the single source of truth)."""
import pathlib
import re

HEADER = pathlib.Path(__file__).resolve().parents[2] / "src" / "core" / "events.h"
_LINE = re.compile(r"^\s*EVT_(\w+)\s*=\s*(\d+)\s*,\s*/\*\s*(.*?)\s*\*/", re.M)


def load(path=HEADER) -> dict:
    """{id: (NAME, description)}"""
    events = {}
    for name, id_, text in _LINE.findall(pathlib.Path(path).read_text()):
        if int(id_) in events:
            raise ValueError(f"duplicate event id {id_} in {path}")
        events[int(id_)] = (name, text)
    return events


EVENTS = load()


def name(id_: int) -> str:
    return EVENTS.get(id_, (f"UNKNOWN_{id_}", ""))[0]
