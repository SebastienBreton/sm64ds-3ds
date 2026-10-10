"""Resolve reviewed CLPS edges by ROM type/extent, preserving live identity.

Ambiguous address-only relocations must have exactly one CLPS owner. Canonical
level storage receives aliases; existing actor storage remains compiler-owned.
"""
import hashlib
import re
import struct
from all_level_data import collect


def resolve(root, ov, spans):
    cfg = root/f'config/arm9/overlays/ov{ov:03}'
    result = {}
    rows = []
    levels = {n: (blob, ranges) for n, _, blob, ranges, _ in collect(root) if blob}
    for at, target, module in re.findall(
            r'^from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:(\S+)',
            (cfg/'relocs.txt').read_text(), re.M):
        at, target = int(at, 16), int(target, 16)
        if not any(lo <= at < hi for lo, hi in spans) or module == f'overlay({ov})':
            continue
        candidates = [int(x) for x in re.findall(r'\d+', module)]
        valid = []
        for owner in candidates:
            c = root/f'config/arm9/overlays/ov{owner:03}'
            base = int(re.search(r'start:0x([0-9a-f]+)', (c/'delinks.txt').read_text())[1], 16)
            blob = (root/f'extracted/overlays/overlay_{owner:04}.bin').read_bytes()
            off = target-base
            if off < 0 or blob[off:off+4] != b'CLPS':
                continue
            stride, count = struct.unpack_from('<HH', blob, off+4)
            assert stride == 8
            size = 8+count*stride
            assert off+size <= len(blob)
            valid.append((owner, blob[off:off+size]))
        assert len(valid) == 1, (ov, hex(at), module, 'CLPS owner count', len(valid))
        owner, data = valid[0]
        # Fixed actors/common bank use their already reviewed concrete labels.
        if owner in (2, 63):
            symbol = f'data_ov{owner:03}_{target:08x}'
            island = None
        else:
            assert owner in levels, 'CLPS has no canonical level import'
            _, ranges = levels[owner]
            island = next((lo for lo, hi in ranges if lo <= target and target+len(data) <= hi), None)
            assert island is not None, 'CLPS not wholly within live level island'
            symbol = f'sm64ds_level_clps_{owner}_{target:08x}'
        result[at] = (target, module, symbol)
        rows.append({'from': hex(at), 'target': hex(target), 'original_module': module,
                     'owner': owner, 'symbol': symbol, 'island': hex(island) if island else None,
                     'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    return result, rows


def alias_level_copy(text, rows):
    for row in {r['symbol']: r for r in rows if r['island']}.values():
        island, target = int(row['island'], 16), int(row['target'], 16)
        label = f'level_{row["owner"]}_{island:08x}:'
        assert text.count(label) == 1
        symbol = row['symbol']
        text = text.replace(label, label+f'\n.global {symbol}\n.set {symbol},level_{row["owner"]}_{island:08x}+{target-island}')
    return text
