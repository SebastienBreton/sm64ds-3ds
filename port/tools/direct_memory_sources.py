#!/usr/bin/env python3
"""Select untranslated originals from a native CMake target's source list.

Numeric game flags and comments are not accesses. Assembly needs an explicit
HAL implementation and is not silently accepted by this audit.
"""
import json
from pathlib import Path
import sys
from hostgen import needs_memory_translation, is_asm

root = Path(sys.argv[1]).resolve()
selected = []
for entry in Path(sys.argv[2]).read_text().splitlines():
    path = Path(entry)
    if not path.is_absolute():
        path = Path(__file__).resolve().parents[1] / path
    path = path.resolve()
    if not path.is_relative_to(root / 'src') or path.suffix not in ('.c', '.cpp'):
        continue
    text = path.read_text()
    if not needs_memory_translation(text):
        continue
    if is_asm(text):
        raise ValueError(f'{path}: DS memory access in untranslated assembly')
    selected.append(str(path))
print(json.dumps(sorted(set(selected))))
