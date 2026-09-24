#!/usr/bin/env python3
"""Capture and summarize X4 Pro flashcard deck-list timings from serial logs.

Capture reads USB serial only; it does not flash firmware or write to the SD card.
The analyze command also accepts logs from scripts/debugging_monitor.py.
"""

import argparse
import collections
import datetime
import re
import tempfile
import time
from pathlib import Path

DEFAULT_LOG = Path(tempfile.gettempdir()) / "crosspoint-flashcard-timings.log"
LINE = re.compile(r"\[(\d+)\] \[(?:DBG|INF|ERR)\] \[([^]]+)\] (.*)")
PERF = re.compile(r"op=([^ ]+) status=([^ ]+) duration_us=(\d+) pool=internal")
DISPLAY_WAIT = re.compile(r"\[(\d+)\]\s+Wait complete:")


def parse_sessions(lines):
    sessions = []
    current = None
    awaiting_display = None
    for line in lines:
        wait = DISPLAY_WAIT.search(line)
        if wait and awaiting_display is not None:
            awaiting_display["display_ms"] = int(wait[1])
            awaiting_display = None
        match = LINE.search(line)
        if not match:
            continue
        device_ms, module, message = int(match[1]), match[2], match[3]
        if module == "FLASH" and "Opening flashcard deck list" in message:
            awaiting_display = None
            current = {
                "start_ms": device_ms,
                "end_ms": None,
                "display_ms": None,
                "decks": [],
                "ops": collections.defaultdict(list),
                "snapshots": 0,
                "replayed_records": 0,
                "errors": [],
            }
            sessions.append(current)
            continue
        if current is None:
            continue
        if module == "FLASH":
            if "Queue load started: deck=" in message:
                current["decks"].append(message.split("deck=", 1)[1].split(" now=", 1)[0])
            elif "Study snapshot loaded:" in message:
                current["snapshots"] += 1
            elif "History replayed:" in message:
                records = re.search(r"records=(\d+)", message)
                if records:
                    current["replayed_records"] += int(records[1])
            elif "Building deck list: decks=" in message:
                current["end_ms"] = device_ms
                awaiting_display = current
                current = None
                continue
            if "Could not " in message or "OOM" in message:
                current["errors"].append(message)
        if module == "FLASHPERF" and (perf := PERF.search(message)):
            current["ops"][perf[1]].append((perf[2], int(perf[3])))
    return sessions


def analyze(path):
    if not path.exists():
        print(f"No log yet: {path}")
        return
    with path.open(errors="replace") as log:
        sessions = parse_sessions(log)
    if not sessions:
        print("No flashcard deck-list opens found. Check the serial port and LOG_LEVEL=2 build.")
        return
    for number, session in enumerate(sessions, 1):
        end = session["end_ms"]
        elapsed = f"{end - session['start_ms']} ms" if end is not None else "incomplete"
        print(f"Open #{number}: {elapsed} from entry to list build")
        if session["display_ms"] is not None:
            print(f"  Display wait completed: {session['display_ms'] - session['start_ms']} ms from entry")
        print("  Decks:", ", ".join(session["decks"]) or "(none)")
        print(f"  Snapshots loaded: {session['snapshots']}; journal records replayed: {session['replayed_records']}")
        for operation, values in session["ops"].items():
            times = [duration for _, duration in values]
            failed = sum(status != "ok" for status, _ in values)
            print(
                f"  {operation:19s} {len(values):2d} calls"
                f"  sum={sum(times) / 1000:8.1f} ms  max={max(times) / 1000:8.1f} ms"
                + (f"  errors={failed}" if failed else "")
            )
        for error in session["errors"]:
            print("  ERROR:", error)
        print("  Note: load_study_queue includes cache validation and history replay; do not add nested totals.")


def capture(port, duration, path):
    import serial

    print(f"Read-only capture: {port} for up to {duration}s → {path}", flush=True)
    try:
        with serial.Serial(port, 115200, timeout=0.5) as device, path.open("w", buffering=1) as log:
            deadline = time.monotonic() + duration
            while time.monotonic() < deadline:
                raw = device.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip()
                timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")
                log.write(f"{timestamp} {line}\n")
                if "[FLASH]" in line or ("[FLASHPERF]" in line and "pool=internal" in line):
                    print(line, flush=True)
    except KeyboardInterrupt:
        pass
    analyze(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("capture", "analyze"))
    parser.add_argument("--port", default="/dev/ttyACM0", help="USB serial device (capture only)")
    parser.add_argument("--duration", type=int, default=120, help="Capture duration in seconds")
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG, help="Raw log path (outside the repository by default)")
    args = parser.parse_args()
    if args.action == "capture":
        capture(args.port, args.duration, args.log)
    else:
        analyze(args.log)


if __name__ == "__main__":
    main()
