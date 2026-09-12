#!/usr/bin/env python3
"""Generate deterministic flashcard decks for X4 Pro performance measurements."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

DEFAULT_SIZES = (100, 1000, 2000)
CACHE_SUFFIXES = ("cards", "cards.tmp", "records.tmp", "text.tmp")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create benchmark CSV decks under an SD card's /flashcards directory"
    )
    parser.add_argument("sd_root", type=Path, help="Mounted SD card root")
    parser.add_argument(
        "--sizes",
        type=int,
        nargs="+",
        default=DEFAULT_SIZES,
        help="Card counts to generate (default: 100 1000 2000)",
    )
    parser.add_argument(
        "--text-scale",
        type=int,
        default=1,
        help="Repeat answer detail to increase CSV and cache size (default: 1)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing benchmark decks",
    )
    parser.add_argument(
        "--reset-cache",
        action="store_true",
        help="Remove only the generated decks' .cards caches to force a cold import",
    )
    parser.add_argument(
        "--reset-history",
        action="store_true",
        help="Remove only the generated decks' review histories",
    )
    return parser.parse_args()


def deck_key(filename: str) -> int:
    value = 1469598103934665603
    for byte in filename.lower().encode("ascii"):
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def card_text(index: int, text_scale: int) -> tuple[str, str]:
    front = f"Benchmark question {index}: explain concept {index % 37} and give one practical example."
    detail = (
        f"Answer {index}: concept {index % 37} has a deterministic explanation, "
        f'a quoted term "sample {index % 11}", and a practical consequence. '
    )
    back = detail + ("Additional detail for streaming and wrapping measurements. " * text_scale)
    if index % 25 == 0:
        front += "\nThis question contains a second line."
    if index % 40 == 0:
        back += "\nThis answer contains a second line and UTF-8 text: Größe."
    return front, back


def remove_generated_state(sd_root: Path, filename: str, reset_cache: bool, reset_history: bool) -> None:
    cache_dir = sd_root / ".crosspoint" / "flashcards"
    key = deck_key(filename)
    if reset_cache:
        for suffix in CACHE_SUFFIXES:
            path = cache_dir / f"{key:016x}.{suffix}"
            if path.exists():
                path.unlink()
                print(f"Removed {path}")
    if reset_history:
        path = cache_dir / f"{key:016x}.history"
        if path.exists():
            path.unlink()
            print(f"Removed {path}")


def main() -> None:
    args = parse_args()
    if args.text_scale < 1:
        raise SystemExit("--text-scale must be at least 1")
    if not args.sizes or any(size < 1 or size > 2000 for size in args.sizes):
        raise SystemExit("Every deck size must be between 1 and 2000")

    sd_root = args.sd_root.expanduser().resolve()
    flashcards_dir = sd_root / "flashcards"
    flashcards_dir.mkdir(parents=True, exist_ok=True)

    for size in args.sizes:
        filename = f"benchmark-{size}.csv"
        path = flashcards_dir / filename
        if path.exists() and not args.force:
            raise SystemExit(f"{path} already exists; pass --force to overwrite benchmark decks")
        with path.open("w", encoding="utf-8", newline="") as output:
            writer = csv.writer(output, lineterminator="\r\n")
            writer.writerow(("front", "back"))
            for index in range(1, size + 1):
                writer.writerow(card_text(index, args.text_scale))
        remove_generated_state(sd_root, filename, args.reset_cache, args.reset_history)
        print(f"Generated {path}: {size} cards, {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
