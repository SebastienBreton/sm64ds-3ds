#!/usr/bin/env python3
"""Emit reviewed actor/dependency originals while retaining native C language.

The source manifest is a closure, not a synthetic actor implementation. GCC
primary address points use the common reviewed ABI adapter. Source-specific
compile adjustments are bounded and keep original guarded assignments/calls.
"""
import argparse
import json
import re
from pathlib import Path
import tempfile
import hostgen
from level_actor_cpp_adapters import adapt
from native_vtable_stores import adapt_reviewed, adapt as vtable_adapt


NEW_SOURCES = set(Path(__file__).with_name('native_level_actor_sources_new.txt').read_text().split())


def vptr_adapt(source, text):
    """levels-render sources: every explicit _ZTV store targets a GCC primary
    vtable (no ROM-layout table is imported for them), whose address point is
    +8 on ARM32. Checked after link by levels-render/tools/check_vtables.py."""
    if source not in NEW_SOURCES:
        return adapt_reviewed(text)
    tables = set(re.findall(r'\b_ZTV\w+', text))
    # Extra reviewed shape: `*(void **)obj = [&]_ZTVx;` (void-pointer store).
    text = re.sub(r'(\*\s*\(\s*void\s*\*\s*\*\s*\)\s*\w+\s*=\s*)&?(_ZTV\w+)\b(?!\s*(?:\+|\[))',
                  lambda m: m[1] + '(void *)((char *)&' + m[2] + ' + 8)', text)
    return vtable_adapt(text, tables)


def generate(root, source, directory):
    original = root / source
    generated, rewrites = hostgen.emit(original, directory, root, True)
    native_c = (original.suffix == '.c' and rewrites == 0
                and not original.read_text().lstrip().startswith('//cpp')
                and not re.search(r'\*\s*\(vu(?:8|16|32|64)\s*\*\s*\)\s*0x0?4[0-9a-fA-F]+', original.read_text()))
    if native_c:
        text, records = vptr_adapt(source, original.read_text())
        suffix = '.c'
    else:
        text, _ = adapt(source, generated.read_text())
        text, records = vptr_adapt(source, text)
        suffix = '.cpp'
    return str(Path(source).with_suffix(suffix)), text, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--plan', action='store_true')
    parser.add_argument('--source')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    sources = json.loads(Path(__file__).with_suffix('.json').read_text())['sources']
    with tempfile.TemporaryDirectory(prefix='native-actors-') as staging:
        if args.plan:
            rows = []
            for source in sources:
                relative, _, _ = generate(args.root, source, Path(staging))
                rows.append({'source': source, 'generated': relative})
            print(json.dumps(rows))
        else:
            assert args.source in sources and args.output
            relative, text, _ = generate(args.root, args.source, Path(staging))
            assert Path(relative).suffix == args.output.suffix
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(text)


if __name__ == '__main__':
    main()
