#!/usr/bin/env python3
"""Publish a freshly generated tree without invalidating unchanged CMake inputs."""
from pathlib import Path
import argparse


def sync(source: Path, destination: Path, preserve=()):
    source, destination = source.resolve(), destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Generated trees must be separate directories')
    if not source.is_dir():
        raise ValueError('Generated source tree does not exist')
    keep = {Path(name) for name in preserve}
    if any(path.is_absolute() or '..' in path.parts for path in keep):
        raise ValueError('Preserved paths must be relative')
    files = {path.relative_to(source): path for path in source.rglob('*') if path.is_file()}
    changed = removed = unchanged = 0
    for relative, path in files.items():
        output = destination / relative
        content = path.read_bytes()
        if output.is_file() and output.read_bytes() == content:
            unchanged += 1
            continue
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(content)
        changed += 1
    for path in destination.rglob('*'):
        if path.is_file() and path.relative_to(destination) not in files and path.relative_to(destination) not in keep:
            path.unlink()
            removed += 1
    return changed, removed, unchanged


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--preserve', action='append', default=[])
    args = parser.parse_args()
    changed, removed, unchanged = sync(args.source, args.destination, args.preserve)
    print(f'Generated tree: changed={changed} removed={removed} unchanged={unchanged}')
