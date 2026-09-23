#!/usr/bin/env python3
"""Copy flashcard journals from a mounted reader and summarize them in the terminal."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import json
import math
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import time
import zlib

RECORD = struct.Struct("<IHHBBBBQQffqqiI")
MAGIC = 0x31484346  # FCH1
HISTORY_DIR = Path(".crosspoint/flashcards")
DEFAULT_OUTPUT = Path.home() / ".local/share/crosspoint-reader/flashcard-history"


def deck_key(filename: str) -> str:
    # Matches FlashcardStore::deckKey: hash the complete CSV filename, not the stem.
    value = 1469598103934665603
    for byte in filename.encode("utf-8"):
        byte = byte + 32 if 65 <= byte <= 90 else byte
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def mount_candidates() -> list[Path]:
    user = os.environ.get("USER", "")
    parents = [Path("/run/media") / user, Path("/media") / user, Path("/Volumes")]
    return [child for parent in parents if parent.is_dir() for child in parent.iterdir() if child.is_mount()]


def find_source(explicit: Path | None) -> Path | None:
    if explicit is not None:
        root = explicit.expanduser()
        directory = root / HISTORY_DIR
        return root if directory.is_dir() and any(directory.glob("*.history")) else None
    candidates = [root for root in mount_candidates()
                  if (root / HISTORY_DIR).is_dir() and any((root / HISTORY_DIR).glob("*.history"))]
    if len(candidates) > 1:
        raise ValueError("Multiple reader drives found: " + ", ".join(map(str, candidates)) + "; use --source")
    return candidates[0] if candidates else None


def wait_for_source(explicit: Path | None, timeout: float, poll: float = 1.0) -> Path:
    deadline = time.monotonic() + timeout
    while True:
        source = find_source(explicit)
        if source is not None:
            return source
        if time.monotonic() >= deadline:
            raise TimeoutError("No mounted .crosspoint/flashcards/*.history found; use --source for a custom mount path")
        time.sleep(min(poll, max(0, deadline - time.monotonic())))


def deck_names(root: Path) -> dict[str, str]:
    directory = root / "flashcards"
    if not directory.is_dir():
        return {}
    return {deck_key(path.name): path.stem for path in directory.iterdir()
            if path.is_file() and path.suffix.lower() == ".csv"}


def copy_snapshot(source: Path, output: Path) -> Path:
    histories = sorted((source / HISTORY_DIR).glob("*.history"))
    if not histories:
        raise ValueError(f"No .history files found under {source / HISTORY_DIR}")
    names = deck_names(source)
    if output.resolve().is_relative_to(source.resolve()):
        raise ValueError("Output must be outside the reader drive")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".flashcards-", dir=output) as staging:
        stage = Path(staging)
        for path in histories:
            if not path.is_file():
                raise OSError(f"History disappeared during copy: {path}")
            # Check that the journal did not change while being copied (or disconnect).
            with path.open("rb") as src, (stage / path.name).open("wb") as dst:
                before = os.fstat(src.fileno())
                shutil.copyfileobj(src, dst)
                after = os.fstat(src.fileno())
                if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or dst.tell() != after.st_size:
                    raise OSError(f"History changed during copy: {path}; retry after leaving the review screen")
        (stage / "decks.json").write_text(json.dumps(names, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        base = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        snapshot = output / base
        index = 1
        while snapshot.exists():
            snapshot = output / f"{base}-{index}"
            index += 1
        # Only publish a complete snapshot; TemporaryDirectory cleans up failures.
        stage.rename(snapshot)
        return snapshot


def read_events(path: Path) -> tuple[list[tuple[int, int, int, int, int]], str | None]:
    """Return (type, rating, card-id, UTC timestamp, version), stopping at a damaged tail."""
    events = []
    with path.open("rb") as stream:
        while data := stream.read(RECORD.size):
            offset = stream.tell() - len(data)
            if len(data) != RECORD.size:
                return events, f"{path.name}: incomplete record at byte {offset}"
            (magic, version, size, kind, rating, phase, step, first, second,
             stability, difficulty, timestamp, due, introduced, checksum) = RECORD.unpack(data)
            if (magic != MAGIC or version not in (1, 2, 3) or size != RECORD.size
                    or zlib.crc32(data[:56]) != checksum or kind not in (1, 2, 3)
                    or (kind == 3 and version < 3) or (kind == 2 and rating not in (1, 3))
                    or timestamp < 0 or due < 0 or introduced < 0
                    or ((kind == 2 or (kind == 3 and phase != 1))
                        and (not math.isfinite(stability) or stability <= 0
                             or not math.isfinite(difficulty) or not 1 <= difficulty <= 10))):
                return events, f"{path.name}: invalid record at byte {offset}"
            if version != 1 and (phase not in (1, 2, 3, 4) or step >= 8 or (kind == 1 and phase != 1)
                                 or (kind == 2 and phase == 1)
                                 or (kind == 3 and phase == 1 and step != 0)):
                return events, f"{path.name}: invalid phase at byte {offset}"
            # The firmware uses the same field for a revert's *previous* review time.
            events.append((kind, rating, first << 64 | second, timestamp, version))
    return events, None


def summarize(snapshot: Path, days: int) -> str:
    names = json.loads((snapshot / "decks.json").read_text(encoding="utf-8"))
    counts = Counter()
    daily = Counter()
    rows = []
    warnings = []
    for path in sorted(snapshot.glob("*.history")):
        events, warning = read_events(path)
        if warning:
            warnings.append(warning + " (statistics use only the valid prefix; raw file retained)")
        reviews = []
        per_card = defaultdict(list)
        introductions = set()
        undos = 0
        for kind, rating, card, timestamp, _version in events:
            if kind == 1:
                introductions.add(card)
            elif kind == 2:
                per_card[card].append(len(reviews))
                reviews.append([rating, timestamp, True])
            else:
                undos += 1
                if per_card[card]:
                    reviews[per_card[card].pop()][2] = False
                else:
                    warnings.append(f"{path.name}: correction without preceding review")
        active = [review for review in reviews if review[2]]
        again = sum(rating == 1 for rating, _, _ in active)
        good = len(active) - again
        counts.update(introduced=len(introductions), reviews=len(active), again=again, good=good, undos=undos)
        for _, timestamp, _ in active:
            daily[datetime.fromtimestamp(timestamp, timezone.utc).date()] += 1
        rows.append((names.get(path.stem, path.stem), len(introductions), len(active), good, again, undos))

    lines = [f"Snapshot: {snapshot}", "Dates: UTC | Ratings: undone reviews excluded", "",
             f"{'Deck':<28} {'New':>5} {'Reviews':>8} {'Good':>6} {'Again':>6} {'Undo':>5}", "-" * 65]
    for name, new, reviews, good, again, undos in rows:
        lines.append(f"{name[:28]:<28} {new:>5} {reviews:>8} {good:>6} {again:>6} {undos:>5}")
    lines += ["-" * 65,
              f"{'Total':<28} {counts['introduced']:>5} {counts['reviews']:>8} {counts['good']:>6} {counts['again']:>6} {counts['undos']:>5}",
              "", f"Reviews per day (last {days} UTC days):"]
    today = datetime.now(timezone.utc).date()
    max_count = max((daily[today - timedelta(days=i)] for i in range(days)), default=0)
    for i in reversed(range(days)):
        day = today - timedelta(days=i)
        count = daily[day]
        bar = "#" * (round(count * 40 / max_count) if max_count else 0)
        lines.append(f"{day.isoformat()} | {bar:<40} {count}")
    lines.extend(f"WARNING: {message}" for message in warnings)
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Mounted SD root (auto-detected if omitted)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"Snapshot directory (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--timeout", type=float, default=30, help="Seconds to wait for the USB drive (default: 30)")
    parser.add_argument("--days", type=int, default=14, help="Days in the UTC activity chart (default: 14)")
    parser.add_argument("--snapshot", type=Path, help="Analyze an existing local snapshot without a device")
    args = parser.parse_args()
    if args.timeout < 0 or args.days < 1:
        parser.error("--timeout must be non-negative and --days must be positive")
    try:
        if args.snapshot is None:
            print("Waiting for reader drive...", file=sys.stderr)
            source = wait_for_source(args.source, args.timeout)
            snapshot = copy_snapshot(source, args.output.expanduser())
        else:
            snapshot = args.snapshot.expanduser()
        print(summarize(snapshot, args.days))
    except (OSError, ValueError, TimeoutError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
