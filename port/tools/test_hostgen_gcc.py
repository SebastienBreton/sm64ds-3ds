"""GNU ARM link-level regression for the consolidated-source ABI adapter."""
import pathlib
import subprocess
import tempfile
import unittest
from hostgen import rewrite_local_externs

BIN = pathlib.Path("/opt/devkitpro/devkitARM/bin")

class LocalExternTests(unittest.TestCase):
    def test_conflicting_views_resolve_to_original_symbols(self):
        source = r"""
struct Hero { void step(); };
#define LOOKUP(index) sine[index]
extern "C" {
int first() {
    extern struct State;
    extern int value;
    extern int read_value(int *);
    extern short sine[];
    // read_value must remain in comments.
    const char *text = "read_value";
    return read_value(&value) + LOOKUP(0) + (text[0] == 'r');
}
int second() {
    extern int value[];
    extern int read_value(void *);
    extern short sine[];
    return read_value(value) + LOOKUP(0);
}
void third(Hero *hero) {
    extern void (Hero::*steps[])();
    (hero->*steps[0])();
}
}
"""
        transformed, count = rewrite_local_externs(source)
        self.assertEqual(count, 5)
        self.assertIn('"read_value"', transformed)
        self.assertIn('// read_value must remain in comments.', transformed)
        self.assertNotIn('extern struct State;', transformed)
        definitions = r"""
struct Hero { void step(); };
void Hero::step() {}
extern "C" {
int value = 42;
short sine[1] = {7};
int read_value(int *ptr) { return *ptr; }
void (Hero::*steps[])() = { &Hero::step };
}
"""
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, content in (("calls", transformed), ("defs", definitions)):
                path = root / (name + ".cpp")
                path.write_text(content)
                subprocess.run([str(BIN / "arm-none-eabi-g++"), "-std=c++17", "-fno-exceptions", "-fno-rtti", "-c", str(path), "-o", str(root / (name + ".o"))], check=True, capture_output=True)
            obj = root / "linked.o"
            subprocess.run([str(BIN / "arm-none-eabi-ld"), "-r", str(root / "calls.o"), str(root / "defs.o"), "-o", str(obj)], check=True, capture_output=True)
            undefined = subprocess.check_output([str(BIN / "arm-none-eabi-nm"), "-u", str(obj)], text=True)
            self.assertEqual(undefined.strip(), "")

    def test_cpp_overloads_are_not_given_raw_c_names(self):
        source = "int f() { extern int overloaded(int); return overloaded(2); }"
        out, count = rewrite_local_externs(source)
        self.assertEqual(count, 0)
        self.assertEqual(source, out)

if __name__ == "__main__":
    unittest.main()
