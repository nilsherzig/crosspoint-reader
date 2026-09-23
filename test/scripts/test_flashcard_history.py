"""Host-side tests for scripts/flashcard_history.py (python3 -m unittest discover -s test/scripts)."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import flashcard_history as history


def record(version, kind, rating=0, phase=1, card=1, timestamp=1700000000):
    data = bytearray(history.RECORD.pack(history.MAGIC, version, 60, kind, rating, phase, 0,
                                         card, 42, 1.5, 5.0, timestamp, timestamp, 19000, 0))
    data[56:] = zlib.crc32(data[:56]).to_bytes(4, "little")
    return bytes(data)


class HistoryTests(unittest.TestCase):
    def test_versions_crc_and_damaged_tail(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "deck.history"
            path.write_bytes(record(1, 1) + record(2, 2, 3, 2) + record(3, 2, 1, 3) + b"partial")
            events, warning = history.read_events(path)
            self.assertEqual([event[0] for event in events], [1, 2, 2])
            self.assertIn("incomplete record at byte 180", warning)
            bad = bytearray(record(2, 2, 3, 2))
            bad[40] ^= 1
            path.write_bytes(record(1, 1) + bad + record(3, 2, 1, 3))
            events, warning = history.read_events(path)
            self.assertEqual(len(events), 1)
            self.assertIn("invalid record at byte 60", warning)

    def test_undo_excludes_only_last_rating_and_keeps_introduction(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "decks.json").write_text(json.dumps({"abc": "Example"}))
            (root / "abc.history").write_bytes(record(3, 1) + record(3, 2, 1, 2)
                                                   + record(3, 3, phase=1)
                                                   + record(3, 2, 3, 3))
            output = history.summarize(root, 1)
            self.assertIn("Example", output)
            self.assertIn("Total                            1        1      1      0     1", output)

    def test_waits_for_mount_content(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            directory = root / history.HISTORY_DIR
            directory.mkdir(parents=True)
            def finish_mount(_delay):
                (directory / "abc.history").write_bytes(record(1, 1))
            with patch.object(history.time, "sleep", side_effect=finish_mount):
                self.assertEqual(history.wait_for_source(root, 2), root)

    def test_timeout_and_ambiguous_drives(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(TimeoutError):
                history.wait_for_source(root, 0)
            a, b = root / "a", root / "b"
            for drive in (a, b):
                directory = drive / history.HISTORY_DIR
                directory.mkdir(parents=True)
                (directory / "abc.history").write_bytes(record(1, 1))
            with patch.object(history, "mount_candidates", return_value=[a, b]):
                with self.assertRaisesRegex(ValueError, "Multiple reader drives"):
                    history.find_source(None)

    def test_snapshot_is_local_complete_and_does_not_change_device(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "device"
            source = root / history.HISTORY_DIR
            source.mkdir(parents=True)
            (root / "flashcards").mkdir()
            (root / "flashcards" / "Demo.csv").write_text("front,back\n")
            original = record(3, 1) + record(3, 2, 3, 2)
            (source / f"{history.deck_key('Demo.csv')}.history").write_bytes(original)
            output = Path(temp) / "output"
            with self.assertRaisesRegex(ValueError, "outside the reader"):
                history.copy_snapshot(root, root / "accidental-output")
            first = history.copy_snapshot(root, output)
            second = history.copy_snapshot(root, output)
            self.assertNotEqual(first, second)
            self.assertEqual((first / f"{history.deck_key('Demo.csv')}.history").read_bytes(), original)
            self.assertEqual((source / f"{history.deck_key('Demo.csv')}.history").read_bytes(), original)
            self.assertIn("Demo", history.summarize(first, 1))


if __name__ == "__main__":
    unittest.main()
