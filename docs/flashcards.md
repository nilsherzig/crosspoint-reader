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
```

This is intentionally a small TOML subset: blank lines, `#` comments, and the three scalar keys above are supported. Unknown keys, unsupported TOML syntax, and out-of-range values block studying until corrected. Valid ranges are 0–1,000 new cards, 0.70–0.99 retention, and 1–365,000 days.

The new-card limit applies independently to each deck and UTC calendar day. A new card consumes quota when it is first displayed, not when a session is opened or when the card is first rated. Due cards are never limited.

## Scheduling and sessions

The scheduler is a scalar C++ port of the FSRS-6 scheduling equations using the 21 default parameters from `fsrs-rs` 5.2.0. Only `Again` (FSRS rating 1) and `Good` (rating 3) are exposed. Parameter optimization, training, and interval fuzzing are intentionally omitted.

A session has strict phases:

1. all due cards, ordered by due timestamp and then CSV order;
2. today's introduced and selected new cards, in CSV order.

`Again` updates FSRS immediately, sets the due timestamp to now, and appends the card to the end of its current phase. A later same-session rating therefore has zero elapsed days and takes the FSRS short-term path. `Good` schedules at least one day ahead. The new phase cannot start while an `Again` repeat remains in the due phase.

The hardware RTC must have been synchronized before studying. Deck browsing and import still work without a valid clock. Day boundaries and due timestamps use UTC so timezone-setting changes do not alter scheduling.

On the front, tapping the card or pressing either page-side button reveals the answer. On the answer, the left/back page-side button or left touch action selects `Again`; the right/forward page-side button or right touch action selects `Good`. Back and the device's global Home behavior leave the session.

## Identity, import, and persistence

The CSV is authoritative. Parsed text and fixed-size card records are cached under `/.crosspoint/flashcards/`. A deck is reparsed only when its FAT modification date/time or byte size changes. Cache payloads have CRC-32 protection and are rebuilt from CSV when damaged.

A card identity is a deterministic 128-bit fingerprint over the exact decoded front and back byte strings, including whitespace and line endings. Editing either side creates a new card. Removing a row removes it from the active deck, but its history remains. Re-adding identical content to the same deck restores its state.

A deck identity is a deterministic case-folded 64-bit hash of the CSV filename. Renaming a deck, except for changing ASCII letter case, therefore creates a separate deck identity and does not automatically migrate history.

Every introduction and review is appended to the deck's `.history` journal. Every review record contains the card fingerprint, UTC timestamp, rating, resulting FSRS memory state, and due timestamp. Removed cards' records are retained. Each fixed-size record has a CRC-32; an incomplete or damaged tail is truncated to the last valid record before new reviews are appended.

## Memory model

CSV import is streaming. It keeps the current row, an 8 KiB Bloom filter, and small buffered-I/O blocks in memory; card records and decoded text go to separate temporary SD files before the final cache is assembled. Bloom-filter hits are verified against the temporary records, so the filter cannot reject a unique card by itself.

Studying loads fixed-size metadata and 16-bit queue indexes for only the selected deck, while front and back text are fetched one card at a time. The vectors are pre-reserved and bounded by the 2,000-card limit. A process-lifetime static pool was rejected because it would permanently reserve the worst-case size even outside the app; the selected-deck allocations are released in `onExit()`.

## Revisit-friendly decisions

The following implementation choices were not inherent in the original product requirements and can be revised independently:

- decks are top-level CSV files only;
- filename-based deck identity means renames do not migrate progress;
- duplicate cards invalidate the whole deck instead of being merged or treated as separate cards;
- empty front or back fields are rejected;
- due ties and new cards follow CSV order;
- quota is consumed on first display and uses UTC day boundaries;
- malformed config blocks all study sessions rather than falling back per invalid key;
- card/deck/row limits are fixed safety bounds;
- cache invalidation uses FAT timestamp plus size, so an edit preserving both can require touching the file or deleting `/.crosspoint/flashcards/`;
- cards are plain text; HTML, Markdown, images, tags, hints, and reverse cards are not interpreted;
- only the fixed FSRS-6 defaults, deterministic intervals without fuzzing, `Again`, and `Good` are supported;
- review history is never compacted so it remains available for a future optimizer;
- no settings UI or per-deck configuration is provided yet.
