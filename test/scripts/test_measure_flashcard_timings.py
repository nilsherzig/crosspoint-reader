"""Host-side tests for scripts/measure_flashcard_timings.py."""

import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import measure_flashcard_timings as timings


class TimingTests(unittest.TestCase):
    def test_two_opens_count_internal_pool_only_and_do_not_double_count_nested_operations(self):
        lines = [
            "[1000] [DBG] [FLASH] Opening flashcard deck list",
            "[1010] [DBG] [FLASH] Queue load started: deck=Example now=1700000000",
            "[1030] [DBG] [FLASH] Study snapshot loaded: deck=123 history_bytes=600",
            "[1032] [DBG] [FLASH] History replayed: deck=123 records=0 bytes=600",
            "[1040] [DBG] [FLASHPERF] op=validate_cache status=ok duration_us=10000 pool=internal",
            "[1041] [DBG] [FLASHPERF] op=validate_cache status=ok duration_us=10000 pool=psram",
            "[1050] [DBG] [FLASHPERF] op=load_study_queue status=ok duration_us=40000 pool=internal",
            "[1060] [DBG] [FLASH] Building deck list: decks=1",
            "[1090]   Wait complete: 8179_DRF (20 ms)",
            "[1100] [DBG] [FLASH] Opening flashcard deck list",
            "[1110] [DBG] [FLASH] History replayed: deck=123 records=2 bytes=720",
            "[1120] [DBG] [FLASH] Building deck list: decks=1",
            "[1160]   Wait complete: 8179_DRF (20 ms)",
        ]
        first, second = timings.parse_sessions(lines)
        self.assertEqual(first["end_ms"] - first["start_ms"], 60)
        self.assertEqual(first["display_ms"] - first["start_ms"], 90)
        self.assertEqual(first["decks"], ["Example"])
        self.assertEqual(first["snapshots"], 1)
        self.assertEqual(first["ops"]["validate_cache"], [("ok", 10000)])
        self.assertEqual(first["ops"]["load_study_queue"], [("ok", 40000)])
        self.assertEqual(second["replayed_records"], 2)
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "capture.log"
            log.write_text("\n".join(lines))
            with contextlib.redirect_stdout(io.StringIO()) as output:
                timings.analyze(log)
            self.assertIn("Open #1: 60 ms", output.getvalue())
            self.assertIn("Open #2: 20 ms", output.getvalue())
            self.assertIn("Display wait completed: 60 ms", output.getvalue())
            self.assertIn("do not add nested totals", output.getvalue())

    def test_incomplete_capture_and_missing_log_are_reported(self):
        sessions = timings.parse_sessions([
            "[100] [DBG] [FLASH] Opening flashcard deck list",
            "[110] [ERR] [FLASH] Could not count due/new cards for broken: missing cache",
        ])
        self.assertIsNone(sessions[0]["end_ms"])
        self.assertEqual(len(sessions[0]["errors"]), 1)
        with tempfile.TemporaryDirectory() as temp:
            with contextlib.redirect_stdout(io.StringIO()) as output:
                timings.analyze(Path(temp) / "missing.log")
            self.assertIn("No log yet", output.getvalue())


if __name__ == "__main__":
    unittest.main()
