#!/usr/bin/env python3
"""Generate/synchronise Rec Room mini-game admission from ROM evidence.

The complete overlay 4 base and overlay 6 graph (sources, storage, ctors,
vtables) are already produced by prepare_overlay6_native.py. Admitting a
mini-game only needs three coordinated product inputs, which this tool keeps
in sync from the ROM instead of hand edits:

  port/menu/minigame_imports.inc      SM64DS_MINIGAME rows (actor, factory,
                                      ROM profile, ROM factory, ROM vtable)
  port/menu/wanted_dependencies.txt   factory roots for hostgen
  port/menu/overlay6_native_policy.json  admitted_actor_ids

usage: minigame_admission.py <decomp-root> [--admit all|ID[,ID...]] [--list]
Rows are derived from the retail actor profile table (0x02090864), the ov006
profile record (factory, behaviour, render) and the `_ZTV<class>` symbol; the
existing minigame_imports.audit() then re-verifies every row (factory literal
or unique BL-callee vtable evidence) before anything is written.

Rec Room entries (overlay 5 table data_ov005_020c24d8, 36 x 0x34 bytes) map
36 selectable games onto 30 actors; --list prints that mapping.
"""
import argparse, json, re, struct, sys
from pathlib import Path

ARM9_BASE = 0x02004000
PROFILE_TABLE = 0x02090864
OV6_BASE = 0x020bfec0
OV5_BASE = 0x020bfec0
REC_ROOM_TABLE = 0x020c24d8
NAMES = {  # EU English titles (pause-screen text, melonDS), keyed by Rec Room entry
    0: "Mario's Slides",
    1: 'Memory Match',
    2: 'Bob-omb Squad',
    3: 'Wanted!',
    4: 'Bounce and Pounce',
    5: 'Pair-a-Gone',
    6: 'Snowball Slalom',
    7: 'Loves Me...?',
    8: "Sort or 'Splode",
    9: 'Picture Poker',
    10: 'Bingo Ball',
    11: 'Hide and Boo Seek',
    12: 'Trampoline Time',
    13: 'Mushroom Roulette',
    14: 'Coincentration',
    15: 'Puzzle Panel',
    16: 'Shuffle Shell',
    17: 'Mario Slot',
    18: 'Psyche Out!',
    19: 'Boom Box',
    20: 'Bounce and Trounce',
    21: 'Lucky Stars',
    22: 'Slots Shot',
    23: 'Tox Box Shuffle',
    24: 'Connect the Characters',
    25: 'Pair-a-Gone And On',
    26: 'Lakitu Launch',
    27: 'Which Wiggler?',
    28: 'Shell Smash',
    29: 'Memory Master',
    30: 'Intense Coincentration',
    31: 'Mix-a-Mug',
    32: 'Trampoline Terror',
    33: 'Super Mario Slot',
    34: 'Giant Snowball Slalom',
    35: 'Puzzle Panic',
}


def rec_room_entries(root):
    blob = (root/'extracted/overlays/overlay_0005.bin').read_bytes()
    rows = []
    for index in range(36):
        e = struct.unpack_from('<HH4i5i3i', blob, REC_ROOM_TABLE-OV5_BASE+index*0x34)
        rows.append({'index': index, 'scene': e[0], 'variant': e[2], 'page': index % 4,
                     'slot': index // 4, 'always_unlocked': index < 8,
                     'unlock_bit': None if index < 8 else index-8})
    return rows


def ov6_symbols(root):
    text = (root/'config/arm9/overlays/ov006/symbols.txt').read_text()
    return {n: int(a, 16) for n, a in re.findall(r'^(\w+) kind:\S+ addr:0x([0-9a-f]+)', text, re.M)}


def derive(root, actor, symbols):
    arm9 = (root/'extracted/arm9_dec.bin').read_bytes()
    blob = (root/'extracted/overlays/overlay_0006.bin').read_bytes()
    profile = struct.unpack_from('<I', arm9, PROFILE_TABLE+actor*4-ARM9_BASE)[0]
    if not OV6_BASE <= profile < OV6_BASE+len(blob)-8:
        raise ValueError(f'actor {actor}: profile 0x{profile:x} outside overlay 6')
    factory_address, behavior, render = struct.unpack_from('<IHH', blob, profile-OV6_BASE)
    if (behavior, render) != (actor, actor):
        raise ValueError(f'actor {actor}: profile behaviour/render mismatch')
    factory = [n for n, a in symbols.items() if a == factory_address and n.endswith('_classInit')]
    if len(factory) != 1:
        raise ValueError(f'actor {actor}: factory symbol at 0x{factory_address:x} not unique')
    class_name = factory[0].removesuffix('_classInit')
    vtable = symbols.get('_ZTV'+str(len(class_name))+class_name)
    if vtable is None:
        raise ValueError(f'actor {actor}: no _ZTV symbol for {class_name}')
    return dict(actor=actor, factory=factory[0], profile=profile, rom_factory=factory_address,
                vtable=vtable)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('root', type=Path)
    p.add_argument('--admit', default='')
    p.add_argument('--list', action='store_true')
    a = p.parse_args()
    root = a.root.resolve()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
    import minigame_imports
    entries = rec_room_entries(root)
    inc_path = Path(__file__).resolve().parents[1] / 'menu/minigame_imports.inc'
    inc = inc_path.read_text()
    admitted = {int(x) for x in re.findall(r'^SM64DS_MINIGAME\((\d+),', inc, re.M)}
    if a.list:
        for e in entries:
            print(f"{e['index']:2} scene {e['scene']} page {e['page']} slot {e['slot']} "
                  f"variant 0x{e['variant']:x} unlock {e['unlock_bit']} "
                  f"{'ADMITTED' if e['scene'] in admitted else '-'} {NAMES[e['index']]}")
    if not a.admit:
        return
    scenes = sorted({e['scene'] for e in entries})
    wanted = scenes if a.admit == 'all' else sorted(int(x) for x in a.admit.split(','))
    symbols = ov6_symbols(root)
    new_rows = []
    for actor in wanted:
        if actor in admitted:
            continue
        if actor not in scenes:
            raise ValueError(f'{actor} is not a Rec Room scene')
        d = derive(root, actor, symbols)
        label = next(NAMES[e['index']] for e in entries if e['scene'] == actor)
        label = label.replace('"', '')
        new_rows.append(f'SM64DS_MINIGAME({actor},{d["factory"]},0x{d["profile"]:08x},'
                        f'0x{d["rom_factory"]:08x},0x{d["vtable"]:08x},"{label}")\n')
    if not new_rows:
        print('nothing to admit'); return
    # Insert after the last SM64DS_MINIGAME row (initializer order untouched).
    lines = inc.splitlines(keepends=True)
    last = max(i for i, l in enumerate(lines) if l.startswith('SM64DS_MINIGAME('))
    lines[last+1:last+1] = new_rows
    deps_path = Path(__file__).resolve().parents[1] / 'menu/wanted_dependencies.txt'
    deps = deps_path.read_text().splitlines()
    for row in new_rows:
        factory = row.split(',')[1]
        if factory not in deps:
            deps.append(factory)
    policy_path = Path(__file__).resolve().parents[1] / 'menu/overlay6_native_policy.json'
    policy = json.loads(policy_path.read_text())
    backups = {inc_path: inc, deps_path: deps_path.read_text(), policy_path: policy_path.read_text()}
    new_ids = sorted(admitted | {int(r.split('(')[1].split(',')[0]) for r in new_rows})
    policy['admitted_actor_ids'] = new_ids
    try:
        inc_path.write_text(''.join(lines))
        deps_path.write_text('\n'.join(sorted(set(deps)))+'\n')
        policy_path.write_text(json.dumps(policy, indent=2)+'\n')
        result = minigame_imports.audit(root)  # ROM re-verification of every row
    except Exception:
        for path, text in backups.items():
            path.write_text(text)
        raise
    print(f'admitted {len(new_rows)} new actors; ROM-verified {len(result["imports"])} rows')


if __name__ == '__main__':
    main()
