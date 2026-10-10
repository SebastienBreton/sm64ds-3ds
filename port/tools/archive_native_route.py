#!/usr/bin/env python3
"""Archive six LCD/geometry captures for observed native route checkpoints.

Freshness and hashes prevent stale shared-SD captures from becoming evidence.
This tool checks archive completeness only, not gameplay or DS pixel parity.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

CAPTURES = ('player-native-geometry.bmp', 'player-native-geometry-right.bmp',
            'player-native-upper.bmp', 'player-native-bottom.bmp',
            'player-gpu-left.bmp', 'player-gpu-right.bmp')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--sdmc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--started-unix', type=float, required=True)
    args = parser.parse_args()
    labels = re.findall(r'^REAL_ROUTE checkpoint=([^\s]+)',
                        args.log.read_text(), re.M)
    if len(labels) != len(set(labels)):
        raise ValueError('Duplicate route checkpoints in log')
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for label in labels:
        if not re.fullmatch(r'[A-Za-z0-9_-]+', label):
            raise ValueError('Unsafe checkpoint label ' + label)
        for name in CAPTURES:
            source = args.sdmc / ('real-' + label + '-' + name)
            row = {'checkpoint': label, 'name': source.name}
            if not source.is_file():
                row['status'] = 'MISSING'
            elif source.stat().st_mtime < args.started_unix:
                row['status'] = 'STALE'
                row['mtime_unix'] = source.stat().st_mtime
            else:
                dest = args.output / source.name
                shutil.copy2(source, dest)
                row.update(status='ARCHIVED', bytes=dest.stat().st_size,
                           sha256=hashlib.sha256(dest.read_bytes()).hexdigest())
            rows.append(row)
    complete = bool(labels) and all(r['status'] == 'ARCHIVED' for r in rows)
    report = {'status': 'COMPLETE_ARCHIVE' if complete else 'INCOMPLETE_ARCHIVE',
              'checkpoint_count': len(labels), 'files': rows,
              'source_log_sha256': hashlib.sha256(args.log.read_bytes()).hexdigest(),
              'started_unix': args.started_unix,
              'limits': ['Only checkpoints observed in this log are considered.',
                         'No gameplay completion or DS pixel parity claim.']}
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'], len(labels), 'checkpoints', len(rows), 'captures')
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(main())
