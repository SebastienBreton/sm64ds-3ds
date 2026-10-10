#!/usr/bin/env python3
"""Verify native Stage BSS section sizes and every configured symbol alias."""
import importlib.util
from pathlib import Path
import re
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT / "sm64ds-decomp"
spec = importlib.util.spec_from_file_location("stage_state_bss", REPO.parent / "port/tools/stage_state_bss.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
spans = sorted(module.SPANS)
assert all(a < b for a, b in spans)
assert all(b <= c for (_, b), (c, _) in zip(spans, spans[1:])), "overlapping BSS regions"
config = REPO / "config/arm9"
source = "\n".join((config / name).read_text() for name in ["symbols.txt", "overlays/ov001/symbols.txt"])
expected = {}
for line in source.splitlines():
    match = re.fullmatch(r"(\w+) kind:(\S+) addr:0x([0-9a-f]+)(?: ambiguous)?", line)
    if match and any(a <= int(match[3], 16) < b for a, b in spans):
        assert match[2] == "bss", line
        expected[match[1]] = int(match[3], 16)
obj = ROOT / "build3ds/cmake/CMakeFiles/smoke_player.dir/stage_state_bss.o"
with obj.open("rb") as stream:
    elf = ELFFile(stream)
    symbols = elf.get_section_by_name(".symtab")
    for start, end in spans:
        name = f".bss.stage_state_{start:08x}"
        section = elf.get_section_by_name(name)
        assert section is not None, name
        assert section["sh_type"] == "SHT_NOBITS", name
        assert section["sh_size"] == end-start, name
        assert section["sh_addralign"] >= 4, name
        for symbol, address in expected.items():
            if not start <= address < end:
                continue
            entries = symbols.get_symbol_by_name(symbol)
            assert entries and len(entries) == 1, symbol
            entry = entries[0]
            assert entry["st_info"]["bind"] == "STB_GLOBAL", symbol
            assert entry["st_info"]["type"] == "STT_OBJECT", symbol
            assert elf.get_section(entry["st_shndx"]).name == name, symbol
            assert entry["st_value"] == address-start, symbol
report = (f"PASS: {len(expected)} strong BSS aliases retain exact ROM offsets across "
          f"{sum(b-a for a,b in spans)} bytes in {len(spans)} nonoverlapping sections. "
          "Includes audio allocation masks, both stream list heads and wipe buffers. "
          "Runtime initialization and audio playback are not verified.\n")
(ROOT / "build3ds/validation/stage-bss-verification.txt").write_text(report)
print(report, end="")
