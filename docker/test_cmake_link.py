"""CMake uses response files for large Linux linker commands."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "build3ds"))
from cmake_link import expand_response_files


class CMakeLinkTests(unittest.TestCase):
    def test_response_objects_can_be_replaced_like_inline_objects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "objects.rsp").write_text('"objects/path with spaces.o" @more.rsp')
            (root / "more.rsp").write_text('objects/main3ds.o objects/smoke_player.o')
            expected = ["g++", "objects/path with spaces.o", "objects/main3ds.o",
                        "objects/smoke_player.o", "-o", "game.elf"]
            self.assertEqual(expand_response_files(["g++", "@objects.rsp", "-o", "game.elf"], root), expected)
            self.assertEqual(expand_response_files(expected, root), expected)

    def test_recursive_response_file_fails_explicitly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "cycle.rsp").write_text("@cycle.rsp")
            with self.assertRaisesRegex(ValueError, "recursive linker response"):
                expand_response_files(["g++", "@cycle.rsp"], root)


if __name__ == "__main__":
    unittest.main()
