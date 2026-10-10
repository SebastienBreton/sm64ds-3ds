import tempfile
import unittest
from pathlib import Path
import runner_journal


class JournalTest(unittest.TestCase):
    def test_restores_present_and_absent_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            present, absent = root/'settings', root/'env'
            present.write_bytes(b'original\r\n\x00\xff')
            journal = root/'journal.json'
            runner_journal.save(journal, {present: present.read_bytes(), absent: None})
            present.write_bytes(b'temporary')
            absent.write_text('temporary')
            self.assertTrue(runner_journal.recover(journal, [present, absent]))
            self.assertEqual(present.read_bytes(), b'original\r\n\x00\xff')
            self.assertFalse(absent.exists())
            self.assertFalse(journal.exists())
            self.assertFalse(runner_journal.recover(journal, [present, absent]))

    def test_rejects_foreign_paths_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            allowed, foreign = root/'settings', root/'foreign'
            journal = root/'journal.json'
            runner_journal.save(journal, {allowed: b'original', foreign: b'foreign'})
            allowed.write_bytes(b'temporary')
            with self.assertRaises(ValueError):
                runner_journal.recover(journal, [allowed])
            self.assertEqual(allowed.read_bytes(), b'temporary')
            self.assertFalse(foreign.exists())
            self.assertTrue(journal.exists())


if __name__ == '__main__':
    unittest.main()
