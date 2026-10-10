#!/usr/bin/env python3
"""Comparable read-only ASMP gameplay counters, without pointer equivalence.

Offsets follow NumCoins/GiveCoins/GiveHealth and Player.h. Actor candidates
require the original vptr, ID, non-dying state and self-owned scene-node field.
This scan does not prove arbitrary scene-tree reachability or whole-RAM parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

FIELDS = ('coins', 'health_raw', 'mouth_actor_id', 'actor_counts')

def compare_observations(reference_path, native_log):
    """Compare normalized observations; keep missing checkpoints and fields."""
    reference = json.loads(Path(reference_path).read_text())
    actual = {}
    for match in re.finditer(r'BOB_GAMEPLAY_TELEMETRY (\{[^\n]+\})', Path(native_log).read_text()):
        item = json.loads(match[1]); label = item['checkpoint']
        if label in actual: raise ValueError('duplicate gameplay checkpoint ' + label)
        actual[label] = item
    rows = []
    for expected in reference['samples']:
        label = expected['checkpoint']; observed = actual.get(label)
        diff = {} if observed is None else {
            key: {'DS': expected.get(key), 'native': observed.get(key)}
            for key in FIELDS if expected.get(key) != observed.get(key)}
        rows.append({'checkpoint': label, 'status': 'NOT_REACHED' if observed is None else
                     ('DIFFERENT_FIELDS' if diff else 'IDENTICAL_FIELDS'), 'differences': diff})
    return {'status': 'OBSERVED_GAMEPLAY_COMPARISON', 'rows': rows,
            'counts': {status: sum(row['status'] == status for row in rows)
                       for status in ('IDENTICAL_FIELDS', 'DIFFERENT_FIELDS', 'NOT_REACHED')},
            'reference_sha256': hashlib.sha256(Path(reference_path).read_bytes()).hexdigest(),
            'native_log_sha256': hashlib.sha256(Path(native_log).read_bytes()).hexdigest(),
            'limits': ['Checkpoint labels require the same completed button route phase.',
                       'DS guarded actor candidates and native scene lookup have distinct enumeration domains.',
                       'No pointer, framebuffer or unobserved gameplay-state parity claim.']}

def inspect_ram(path, checkpoint, actors=()):
    data = Path(path).read_bytes()
    assert len(data) == 4 * 1024 * 1024
    def read(address, fmt='I'):
        offset = address - 0x02000000
        assert 0 <= offset <= len(data) - struct.calcsize('<' + fmt)
        return struct.unpack_from('<' + fmt, data, offset)[0]
    player = read(0x0209f394)
    number = read(player + 0x6d8, 'B'); assert number < 4
    mouth = read(player + 0x360)
    counts = {}
    for actor_id, vtable in actors:
        count, offset = 0, 0
        marker = struct.pack('<I', vtable)
        while True:
            offset = data.find(marker, offset)
            if offset < 0: break
            address = offset + 0x02000000
            if (offset % 4 == 0 and offset + 0x50 <= len(data)
                and read(address + 0xc, 'H') == actor_id
                and read(address + 0xe, 'B') < 2
                and read(address + 0x24) == address):
                count += 1
            offset += 4
        counts[str(actor_id)] = count
    return {'schema': 'sm64ds-gameplay-observation-v1', 'checkpoint': checkpoint,
            'coins': sum(read(0x0209f358 + i * 2, 'h') for i in range(read(0x0209f21c, 'B'))),
            'health_raw': read(0x02092144 + number * 2, 'h'),
            'mouth_actor_id': read(mouth + 0xc, 'H') if mouth else 0,
            'actor_counts': counts}

def emit_header(path, actor_ids):
    counts = '\n'.join(f'''  unsigned count_{aid}=0;
  for(dActor_c* it=nullptr;(it=dActor_c::FindWithActorID({aid},it))!=nullptr;)++count_{aid};'''
                       for aid in actor_ids)
    fmt = ','.join(f'\\"{aid}\\":%u' for aid in actor_ids)
    args = ''.join(f',count_{aid}' for aid in actor_ids)
    text = '''// Generated read-only gameplay observation; no game-state writes.
#pragma once
#include "Player.h"
#include "dActor_c.h"
#include <cstdio>
extern "C" short NumCoins();
extern "C" short data_02092144[];
static void collect_gameplay_observation(const char* label,Player* player){
  if(!player)return;
  const auto* mouth=reinterpret_cast<const dActor_c*>(player->mObjInMouth);
'''+counts+f'''
  std::printf("BOB_GAMEPLAY_TELEMETRY {{\\"schema\\":\\"sm64ds-gameplay-observation-v1\\",\\"checkpoint\\":\\"%s\\",\\"coins\\":%d,\\"health_raw\\":%d,\\"mouth_actor_id\\":%u,\\"actor_counts\\":{{{fmt}}}}}\\n",
    label,int(NumCoins()),int(data_02092144[player->mPlayerNo]),mouth?unsigned(mouth->actorID):0{args});
}}
'''
    Path(path).write_text(text)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ram',type=Path)
    parser.add_argument('--checkpoint',default='snapshot')
    parser.add_argument('--actor',action='append',default=[],help='actor ID:retail vtable address')
    parser.add_argument('--emit-header',type=Path)
    parser.add_argument('--reference',type=Path)
    parser.add_argument('--native-log',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    pairs=[tuple(int(v,0) for v in item.split(':')) for item in args.actor]
    if args.reference:
        if not args.native_log or not args.output: parser.error('--reference requires --native-log and --output')
        report = compare_observations(args.reference, args.native_log)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['counts']))
    elif args.emit_header: emit_header(args.emit_header,[p[0] for p in pairs])
    else: print(json.dumps(inspect_ram(args.ram,args.checkpoint,pairs),indent=2))
