#!/usr/bin/env python3
"""Select/emit reviewed ARM32 vptr adaptations without changing source language.

Generated C++ originals use hostgen's explicit --native-primary-vptrs flag.
This pass handles originals that would otherwise compile directly as C/C++. It
changes only bounded primary stores of independently reviewed GCC tables.
"""
import argparse
import json
from pathlib import Path
from native_vtable_stores import adapt_reviewed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--source-list', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.source_list:
        selected = []
        for entry in args.source_list.read_text().splitlines():
            path = Path(entry)
            if not path.is_absolute():
                path = Path(__file__).resolve().parents[1] / path
            path = path.resolve()
            if path.is_relative_to(root / 'src') and path.suffix in ('.c', '.cpp') and path.exists():
                _, records = adapt_reviewed(path.read_text())
                if records:
                    selected.append(str(path))
        print(json.dumps(sorted(set(selected))))
    else:
        assert args.source and args.output
        source = args.source.resolve()
        source.relative_to(root / 'src')
        assert source.suffix == args.output.suffix and source.suffix in ('.c', '.cpp')
        changed, records = adapt_reviewed(source.read_text())
        assert records, 'No reviewed primary-vptr store in selected source'
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(changed)
        print(json.dumps(records))


if __name__ == '__main__':
    main()
