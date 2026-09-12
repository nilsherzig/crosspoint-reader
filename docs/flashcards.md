# Flashcards

Flashcards are an Xteink X4 Pro-only application. The feature is omitted from other device builds rather than exposed in a degraded mode.

## User files

Create `/flashcards/` on the SD card and place one or more `.csv` files directly in it. Each file is one deck; subdirectories are not scanned. The filename without `.csv` is the displayed deck name.

CSV files are UTF-8 RFC 4180 with a required header containing exactly one `front` and one `back` column:

```csv
front,back
"What is 2 + 2?",4
"A field may contain commas","and escaped ""quotes"""
```

A UTF-8 BOM is accepted. Quoted commas, quotes, CRLF, and embedded newlines are supported. Other columns are parsed but ignored. Empty rows are ignored; every card row must have non-empty front and back fields. A duplicate decoded front/back pair makes the deck invalid.

Limits chosen to bound RAM use are:

- 32 decks
- 2,000 cards per deck
- 64 columns per row
- 32 KiB decoded data per row
- 16 KiB per field

## Configuration

`/flashcards/config.toml` is optional. Defaults are used when it is absent:

```toml
new_cards_per_day = 20
desired_retention = 0.90
maximum_interval_days = 36500
learning_steps_minutes = [1, 10]
relearning_steps_minutes = [10]
```

This is intentionally a small TOML subset: blank lines, `#` comments, the three scalar keys, and the two integer arrays above are supported. Unknown keys, unsupported TOML syntax, and out-of-range values block studying until corrected. Valid ranges are 0–1,000 new cards, 0.70–0.99 retention, and 1–365,000 days. Each learning-step array must contain 1–8 strictly increasing whole-minute values between 1 and 10,080.

The new-card limit applies independently to each deck and UTC calendar day. A new card consumes quota when it is first displayed, not when a session is opened or when the card is first rated. Due cards are never limited. When a selected deck has no due or available new cards but still has unseen cards, a numeric picker can add a one-session allowance of 1–2,000 cards. Only cards actually displayed consume that allowance; the configured daily limit is not changed.

## Scheduling and sessions

The scheduler is a scalar C++ port of the FSRS-6 scheduling equations using the 21 default parameters from `fsrs-rs` 5.2.0. Only `Again` (FSRS rating 1) and `Good` (rating 3) are exposed. Parameter optimization and training are intentionally omitted.

New cards move through the configured learning steps: `Again` resets to the first step, while `Good` advances to the next step and graduates from the last step into normal FSRS review. An `Again` on a graduated review card applies the FSRS lapse update and enters the configured relearning steps with the same reset/advance behavior. Step delays use exact UTC timestamps, remain persisted across sessions, and receive a deterministic increase of up to 25%, capped at five minutes.

Graduated FSRS intervals use deterministic per-card/per-review fuzzing. Intervals below 2.5 days are only rounded; longer intervals use the standard progressively narrowing bands (15% through day 7, 10% through day 20, and 5% thereafter) while respecting the configured maximum. The selected due timestamp is persisted, so replay does not depend on regenerating the random choice.

A session has strict phases:

1. all due cards, ordered by due timestamp and then CSV order;
2. today's introduced and selected new cards, in CSV order.

Learning and relearning cards become eligible at their timestamp and take priority once due. Other due and new cards continue while a step delay is running. If no other card remains, the session displays the time until the next learning card and automatically resumes when it becomes due; the user may leave and return later. A later same-day rating uses `elapsedDays = 0` and therefore takes the FSRS short-term path. A graduated `Good` schedules at least one day ahead.

The hardware RTC must have been synchronized before studying. Deck browsing and import still work without a valid clock. Day boundaries and due timestamps use UTC so timezone-setting changes do not alter scheduling. When the clock and configuration are valid, each deck row shows its total, due, and currently available new-card counts; otherwise it shows only the total.

On the front, tapping the card or pressing either page-side button reveals the answer. The revealed view keeps the question in its original upper section and adds the answer below it; both are left-aligned. Tapping anywhere in the left or right half of the card body selects `Again` or `Good`, respectively, as do the matching footer controls and page-side buttons. The header is excluded from these voting regions, and global Back, Home, and control-center gestures are handled before app touch routing.

## Identity, import, and persistence

The CSV is authoritative. Parsed text and fixed-size card records are cached under `/.crosspoint/flashcards/`. A deck is reparsed only when its FAT modification date/time or byte size changes. Cache payloads have CRC-32 protection and are rebuilt from CSV when damaged.

A card identity is a deterministic 128-bit fingerprint over the exact decoded front and back byte strings, including whitespace and line endings. Editing either side creates a new card. Removing a row removes it from the active deck, but its history remains. Re-adding identical content to the same deck restores its state.

A deck identity is a deterministic case-folded 64-bit hash of the CSV filename. Renaming a deck, except for changing ASCII letter case, therefore creates a separate deck identity and does not automatically migrate history.

Every introduction and review is appended to the deck's `.history` journal. Every review record contains the card fingerprint, UTC timestamp, rating, resulting FSRS memory state, learning/relearning phase, step, and due timestamp. Removed cards' records are retained. Each fixed-size record has a CRC-32; an incomplete or damaged tail is truncated to the last valid record before new reviews are appended. Version 1 history records remain readable and are treated as graduated reviews.

## Memory model

CSV import is streaming. It keeps the current row, an 8 KiB Bloom filter, and small buffered-I/O blocks in memory; card records and decoded text go to separate temporary SD files before the final cache is assembled. Bloom-filter hits are verified against the temporary records, so the filter cannot reject a unique card by itself.

Studying loads fixed-size metadata and 16-bit queue indexes for only the selected deck, while front and back text are fetched one card at a time. The vectors are pre-reserved and bounded by the 2,000-card limit. A transient 16-bit pending-step queue adds at most 4 KiB and allows delayed learning cards to re-enter the same session without repeated heap growth. The deck overview reuses one bounded queue allocation while calculating counts sequentially, then releases it before rendering. A process-lifetime static pool was rejected because it would permanently reserve the worst-case size even outside the app; the selected-deck allocations are released in `onExit()`.

## Performance diagnostics

Development builds emit state transitions under `[FLASH]` and structured timings under `[FLASHPERF]`. Timed operations include cold imports, cache validation, history replay, queue construction, card text reads, review writes, and complete screen updates. Store timings also report internal RAM and PSRAM before and after each operation, including the largest allocatable block and the allocator's low-water mark. These diagnostics compile out when debug logging is disabled.

Generate reproducible 100-, 1,000-, and 2,000-card decks on a mounted SD card with:

```bash
python3 scripts/generate_flashcard_benchmark.py /path/to/sd --reset-cache --reset-history
```

Then capture an unfiltered log while showing only flashcard messages in the terminal:

```bash
python3 scripts/debugging_monitor.py /dev/ttyACM0 \
  --filter flash \
  --log-file flashcards-perf.log
```

The monitor still collects the firmware's periodic `[MEM]` samples for its internal-RAM and PSRAM graph before applying the display filter. It does not measure CPU utilization; `[FLASHPERF] duration_us` measures user-relevant device wall time, including SD or display waits where applicable. Compare a first deck-list open after `--reset-cache` with a second open without resetting the cache, then open each generated deck, reveal cards, rate with both outcomes, leave the session, and reopen it to measure history replay. `--reset-history` deletes only benchmark-deck history; omit it when measuring history growth.

## Revisit-friendly decisions

The following implementation choices were not inherent in the original product requirements and can be revised independently:

- decks are top-level CSV files only;
- filename-based deck identity means renames do not migrate progress;
- duplicate cards invalidate the whole deck instead of being merged or treated as separate cards;
- empty front or back fields are rejected;
- due ties and new cards follow CSV order;
- quota is consumed on first display and uses UTC day boundaries;
- learning steps use strictly increasing whole minutes, with 1–8 steps capped at seven days each;
- malformed config blocks all study sessions rather than falling back per invalid key;
- card/deck/row limits are fixed safety bounds;
- cache invalidation uses FAT timestamp plus size, so an edit preserving both can require touching the file or deleting `/.crosspoint/flashcards/`;
- cards are plain text; HTML, Markdown, images, tags, hints, and reverse cards are not interpreted;
- only the fixed FSRS-6 weights and the `Again`/`Good` ratings are supported; fuzzing and learning-step delays are deterministic from card identity and review time;
- review history is never compacted so it remains available for a future optimizer;
- no settings UI or per-deck configuration is provided yet.
