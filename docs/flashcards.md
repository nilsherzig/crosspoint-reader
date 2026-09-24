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
learn_ahead_limit_minutes = 20
desired_retention = 0.90
maximum_interval_days = 36500
learning_steps_minutes = [1, 10]
relearning_steps_minutes = [10]
undo_binding = 0
font_point_size = 12
show_forecast = 0
show_review_count = 1
backup_enabled = 0
backup_review_interval = 500
backup_server_url = "https://files.nilsherzig.com"
backup_directory = "/flashcards-backups"
```

This is intentionally a small TOML subset: blank lines, `#` comments, the scalar keys and two integer arrays above are supported. `undo_binding` selects: 0 = hold screen or either side button (default), 1 = screen only, 2 = either side button, 3 = upper side button, 4 = lower side button, 5 = disabled. Unknown keys, unsupported TOML syntax, and out-of-range values block studying until corrected. Valid ranges are 0–1,000 new cards, 0.70–0.99 retention, and 1–365,000 days. Card text supports the same built-in 12, 14, 16, and 18 pt sizes as the e-book reader. The default 12 pt keeps the existing UI font; larger sizes use the built-in Noto Sans reader fonts. Only card text changes, not the UI chrome. `show_forecast` is 0 (off by default) or 1 (on); `show_review_count` is 1 (on by default) or 0 (off). The learn-ahead limit accepts whole minutes from 0 to 4,294,967,295. Each learning-step array must contain 1–8 strictly increasing whole-minute values between 1 and 10,080.

On the X4 Pro, these values are editable under Settings > Flashcards. Scalar values use bounded pickers; the learn-ahead limit uses a numeric keyboard and accepts whole minutes up to 4,294,967,295. Learning and relearning steps open a dedicated editor where individual ordered steps can be changed, appended, or removed. The undo binding and card font size open option pickers; the session display submenu contains separate toggles for the deck review count and forecast. The Backups submenu configures the HTTPS server, password, destination directory, and the review interval, and offers an immediate backup. The password is stored as `backup_password_obf` (device-tied obfuscation, not encryption), never included in remote backups. Every successful change rewrites `config.toml` in canonical key order, so comments and original formatting are not preserved. Missing files show the defaults; malformed files show the values parsed before the error and defaults for the rest, then are repaired by the next successful change. Save failures leave the displayed value unchanged and are logged.

The new-card limit applies independently to each deck and UTC calendar day. A new card consumes quota when it is first displayed, not when a session is opened or when the card is first rated. Due cards are never limited. When a selected deck has no due or available new cards but still has unseen cards, a numeric picker can add a one-session allowance of 1–2,000 cards. Only cards actually displayed consume that allowance; the configured daily limit is not changed.

## Scheduling and sessions

The scheduler is a scalar C++ port of the FSRS-6 scheduling equations using the 21 default parameters from `fsrs-rs` 5.2.0. Only `Again` (FSRS rating 1) and `Good` (rating 3) are exposed. Parameter optimization and training are intentionally omitted.

New cards move through the configured learning steps: `Again` resets to the first step, while `Good` advances to the next step and graduates from the last step into normal FSRS review. An `Again` on a graduated review card applies the FSRS lapse update and enters the configured relearning steps with the same reset/advance behavior. Step delays use exact UTC timestamps, remain persisted across sessions, and receive a deterministic increase of up to 25%, capped at five minutes.

Graduated FSRS intervals use deterministic per-card/per-review fuzzing. Intervals below 2.5 days are only rounded; longer intervals use the standard progressively narrowing bands (15% through day 7, 10% through day 20, and 5% thereafter) while respecting the configured maximum. The selected due timestamp is persisted, so replay does not depend on regenerating the random choice.

A session has strict phases:

1. all due cards, ordered by due timestamp and then CSV order;
2. today's introduced and selected new cards, in CSV order.

Learning and relearning cards become eligible at their timestamp and take priority once due. Other due and new cards continue while a step delay is running. If no other card remains, learning and relearning cards are shown early when their remaining delay is strictly less than `learn_ahead_limit_minutes` (default: 20). Eligible learn-ahead cards retain their pending queue order; a card that remains in learning after a rating returns at the back instead of jumping ahead because its next delay is shorter. This also applies after reopening a deck, and never brings graduated review cards forward. With a limit of 0, the full delay is respected. Outside the window, the session displays the time until the next learning card and automatically resumes when it enters the window or becomes due; the user may leave and return later. A later same-day rating uses `elapsedDays = 0` and therefore takes the FSRS short-term path. A graduated `Good` schedules at least one day ahead.

The hardware RTC must have been synchronized before studying. Deck browsing works without a valid clock; without a synchronized clock, due and new-card counts are unavailable. Day boundaries and due timestamps use UTC so timezone-setting changes do not alter scheduling. Before the deck list is drawn, each deck's current due and new-card counts are calculated from its card cache, study snapshot, and any journal records after the snapshot. A missing or invalid snapshot is rebuilt from the journal and saved during this initial scan. The list is drawn once with all available counts; opening it may take longer when caches or snapshots need rebuilding. Counts are also updated when a session ends. The due count includes learning and relearning cards waiting for their next step, so leaving a session does not hide its outstanding cards or offer additional new cards in their place.

On the front, tapping the card or pressing either page-side button reveals the answer. The revealed view keeps the question in its original upper section and adds the answer directly below the divider, aligned to the top of its section; both are left-aligned. The question takes only the space its wrapped lines need (up to half the card area), leaving the remaining space for longer answers. Both sides are bounded to complete font lines; text that exceeds the available area ends with an ellipsis rather than overlapping the other side or the footer. Tapping anywhere in the left or right half of the card body selects `Again` or `Good`, respectively, as do the matching footer controls and page-side buttons. The header is excluded from these voting regions, and global Back, Home, and control-center gestures are handled before app touch routing. Holding the screen or a configured side button for a long press undoes the most recent rating, even on the next card or the session-complete/waiting screen. The previously rated card returns with its answer hidden so it can be rated again. A short-lived header message confirms undo. Only the last rating in the current session is undoable; leaving the session clears this option. Undo appends a correction to the history journal so the restored schedule remains correct after reopening; an introduction of the next card, if already displayed, is not undone.

When enabled, the session-complete screen shows the deck's lifetime total of effective reviews (ratings, not distinct cards), including cards later removed from the CSV. Undone ratings do not count. When the optional forecast is enabled, unseen cards remain, and at least one rating exists, it also gives an approximate month when all current cards will have been introduced, based on distinct cards introduced in the current CSV since the first review day. History replay and live ratings maintain these totals for the saved study snapshot without rereading the SD card at completion. The estimate is a lifetime average, not a daily commitment; long pauses or changes to the deck can skew it. **Done** is the only completion action. Additional new cards can only be chosen by deliberately selecting the deck again in the deck list.

## Identity, import, and persistence

The CSV is authoritative. Parsed text and fixed-size card records are cached under `/.crosspoint/flashcards/`. A deck is reparsed only when its FAT modification date/time or byte size changes. Cache payloads have CRC-32 protection and are rebuilt from CSV when damaged.

A card identity is a deterministic 128-bit fingerprint over the exact decoded front and back byte strings, including whitespace and line endings. Editing either side creates a new card. Removing a row removes it from the active deck, but its history remains. Re-adding identical content to the same deck restores its state.

A deck identity is a deterministic case-folded 64-bit hash of the CSV filename. Renaming a deck, except for changing ASCII letter case, therefore creates a separate deck identity and does not automatically migrate history.

Every introduction and review is appended to the deck's `.history` journal. Every review record contains the card fingerprint, UTC timestamp, rating, resulting FSRS memory state, learning/relearning phase, step, and due timestamp. Removed cards' records are retained. Each fixed-size record has a CRC-32; an incomplete or damaged tail is truncated to the last valid record before new reviews are appended. Version 1 history records remain readable and are treated as graduated reviews. Version 2 added learning phase and step; version 3 adds correction events for undone ratings while retaining the same 60-byte record size.

When a study session closes, the current scheduling state is written to a checksummed `.state` snapshot. On the next open, a matching snapshot restores the card states and only journal records appended after it are replayed. A changed CSV cache, a journal shorter than the snapshot offset, a changed record at that offset, or a damaged snapshot makes the reader replay the full journal and recreate the snapshot on session exit. The reader checks only that boundary record so opening a deck stays fast even with a long history. The journal remains the authoritative review history and is the file to back up or restore; snapshots can be deleted safely.

## Back up to Copyparty

Backups are off by default. Under Settings > Flashcards > Backups, set a password (Copyparty's `PW` header; no username), adjust the HTTPS server and destination directory if necessary, and select **Back up now**. Only HTTPS is accepted so the password is not sent over plain HTTP. The server account needs permission to create directories and upload files via WebDAV `MKCOL` and HTTP `PUT`. The reader connects to Wi-Fi and shows an uploaded-file count; a backup is successful only after every file is accepted by the server. Failed or interrupted uploads leave the reminder count unchanged and may leave an incomplete timestamped folder remotely.

A backup goes to `<backup_directory>/<UTC timestamp>/decks/*.csv` and `.../history/*.history`. All source CSV files and history journals (including journals for removed decks) are copied. `.cards` caches are not uploaded because they are regenerated from the CSVs. Restore the CSV files to `/flashcards/` and the history files to `/.crosspoint/flashcards/` on the SD card, preserving filenames. Copy while not studying. `config.toml` and its obfuscated password are deliberately excluded. The flashcard clock must be synchronized to create a UTC backup timestamp.

Turn on **Backup reminders** to be asked after 500 effective reviews across decks by default (configurable from 1 to 1,000,000). Reviews remain individually persisted in their journals. The small counter file under `/.crosspoint/flashcards/` is updated when a review session exits, including via Home. After an unexpected shutdown, any missed reviews in a deck are reconciled when that deck is next opened and closed. A prompt appears only after completing a deck, never mid-session or on Home. Declining postpones it by another configured number of reviews; a successful manual or prompted backup resets the reminder.

## Export review history from USB

On a Linux host, put the reader in USB file-transfer mode, connect it, then run:

```bash
python3 scripts/flashcard_history.py
```

The script waits up to 30 seconds for the mounted drive and its `/.crosspoint/flashcards/` directory. It creates a timestamped, read-only-from-device copy of every `.history` file and prints per-deck counts of introduced cards, effective `Good`/`Again` reviews, undo corrections, and a UTC daily activity chart. Snapshots are saved outside the repository in `~/.local/share/crosspoint-reader/flashcard-history/`. The optional `--source /path/to/mounted/sd` selects a specific drive (also useful with a local SD-card copy), `--timeout 60` extends the wait, `--output /path/to/archive` changes the destination, and `--days 30` changes the chart length. To re-analyze an existing download without a device, use `--snapshot /path/to/archive/TIMESTAMP`.

An unrecognized deck hash means its CSV file is no longer present on the card. Damaged or incomplete journal tails remain in the downloaded raw file; statistics stop at the last valid record and print a warning. The tool never repairs or writes the card. Do not study on the reader while copying, as a concurrent journal append causes the copy to fail rather than producing a mixed snapshot.

## Memory model

CSV import is streaming. It keeps the current row, an 8 KiB Bloom filter, and small buffered-I/O blocks in memory; card records and decoded text go to separate temporary SD files before the final cache is assembled. Bloom-filter hits are verified against the temporary records, so the filter cannot reject a unique card by itself.

Studying loads fixed-size metadata and 16-bit queue indexes for only the selected deck, while front and back text are fetched one card at a time. The vectors are pre-reserved and bounded by the 2,000-card limit. A transient 16-bit pending-step queue adds at most 4 KiB and allows delayed learning cards to re-enter the same session without repeated heap growth. Undo keeps one fixed-size card/queue-position snapshot in the activity; restoring the pending queue reuses its reserved capacity, and previous card text is reread from SD instead of keeping up to 16 KiB of duplicate text in RAM. Before its first draw, the deck list reuses one queue buffer while counting each deck and rebuilding missing snapshots. That buffer is released when counting finishes; a selected-deck queue is moved into the review activity so it is not loaded twice. The `.state` snapshot is streamed through fixed-size stack buffers and adds no full-deck heap allocation. Backup reconciliation happens when a reviewed deck exits; its two alternating, checksummed counter files guard against a torn update. The backup file list is bounded to 128 entries and uploads stream from SD through a small stack buffer rather than buffering entire CSV files in RAM. A process-lifetime static pool was rejected because it would permanently reserve the worst-case size even outside the app; the selected-deck allocations are released in `onExit()`.

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
- settings are global to the X4 Pro and are stored in `/flashcards/config.toml`; per-deck configuration is not provided.
