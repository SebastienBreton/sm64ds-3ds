#!/usr/bin/env python3
"""Read original level/actor banks and prepare a private native-closure inventory.

No factories or fallback bodies are generated. Source copies are original TUs,
not a claim of linker closure, ABI compatibility or native runtime support.
"""
import argparse, collections, hashlib, json, re, shutil, struct
from pathlib import Path
from all_level_data import collect, reset_records
from overlay_import_audit import audit
from common_scenery_profiles import ROWS as COMMON_ROWS


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def analyze(root, level, native_info, *, overlay_inventory=None, numeric_levels=None):
    arm = (root/'extracted/arm9_dec.bin').read_bytes()
    main_base, level_base, common_base = 0x02004000, 0x021111a0, 0x020ad660
    word = lambda address: struct.unpack_from('<I', arm, address-main_base)[0]
    assert 0 <= level < 52
    ov = word(0x020758c8+4*level)
    assert ov == 8+level, 'Unsupported nonstandard level mapping'
    root_address = word(0x02092208+4*level)
    tags = arm[0x02075998-main_base+level*7:0x02075998-main_base+(level+1)*7]
    banks = [word(0x02075804+i*28+v*4) for i,v in enumerate(tags) if v]
    banks += [60] if level in (0x24,0x26,0x28) else [98,102]
    requested = [ov]+banks
    support = {int(i): {'start': hex(int(start,16)), 'size':int(size),
        'numeric_bytes':int(numeric), 'kind':int(kind)} for i,start,size,numeric,kind
        in re.findall(r'\{(\d+),0x([0-9a-f]+),(\d+),(\d+),(\d+)\}', native_info.read_text())}
    assert all(i in support for i in requested)
    image = (root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
    common = (root/'extracted/overlays/overlay_0002.bin').read_bytes()
    common_symbols = {int(address,16): name for name,address in re.findall(
        r'^(\w+) kind:function\S* addr:0x([0-9a-f]+)',
        (root/'config/arm9/overlays/ov002/symbols.txt').read_text(),re.M)}
    handlers = [common_symbols.get(struct.unpack_from('<I', common,
        0x0210cbb8-common_base+i*4)[0]) for i in range(15)]

    def read(address, fmt):
        size = struct.calcsize(fmt)
        assert level_base <= address <= level_base+len(image)-size
        return struct.unpack_from(fmt,image,address-level_base)

    inventory = audit(root,set(requested)|{2}) if overlay_inventory is None else {
        **overlay_inventory, 'overlays': [r for r in overlay_inventory['overlays']
                                        if r['overlay'] in set(requested)|{2}]}
    registered_common = {words[1]&65535 for _,_,_,words in COMMON_ROWS}
    profiles = collections.defaultdict(list)
    for item in inventory['overlays']:
        for p in item['profiles']:
            profiles[p['actor_id']].append({**p,'overlay':item['overlay'],
                'lifecycle_kind':support.get(item['overlay'],{}).get('kind'),
                'registration':'Static resident; current common manifest' if item['overlay']==2 and p['actor_id'] in registered_common
                    else 'Static resident; absent from current common manifest' if item['overlay']==2
                    else 'Installed by supported lifecycle' if support.get(item['overlay'],{}).get('kind')
                    else 'Lifecycle not yet supported',
                'note':'Original profile identity; installation/ABI runtime proof not implied'})
    # Original loaders use u16 raw-index -> actor-ID mapping, not a u32 table.
    def actor(raw):
        assert 0 <= raw < (0x0210ce80-0x0210cbf4)//2
        return struct.unpack_from('<H',common,0x0210cbf4-common_base+raw*2)[0]

    def table(address, area):
        count, entries = read(address,'<H2xI')
        output = []
        for i in range(count):
            at = entries+i*8
            packed, n, pointer = read(at,'<BB2xI')
            kind, star = packed&31, packed>>5
            row = {'address':hex(at),'area':area,'handler':kind,'star_filter':star,
                'count':n,'records_address':hex(pointer),'actors':[]}
            if kind in (0,1,5):
                step = 8 if kind == 5 else 16
                for j in range(n):
                    values = read(pointer+j*step,'<H3h' if kind == 5 else '<H6hH')
                    raw = values[0]&511 if kind == 5 else values[0]
                    if kind == 5 and raw == 511:
                        row.setdefault('minimap_sentinels',0)
                        row['minimap_sentinels'] += 1
                        continue
                    aid = actor(raw)
                    row['actors'].append({'record':j,'raw':raw,'actor_id':aid,
                        'position_units':list(values[1:4]),
                        'rotation_units':None if kind == 5 else list(values[4:7]),
                        'param':values[0]>>9 if kind == 5 else values[-1],
                        'profile_candidates':profiles[aid]})
                if kind == 1:
                    row['selection_note']='Entrances inventory only; runtime loads the chosen entrance subset and controller332'
            elif kind in (6,9,10):
                expected = {6:'_Z25LoadTeleportSourceObjects',
                            9:'_Z15LoadDoorObjects',10:'_Z15LoadExitObjects'}[kind]
                assert handlers[kind] and handlers[kind].startswith(expected)
                row['loader_symbol'] = handlers[kind]
                for j in range(n):
                    if kind == 6:
                        x,y,z,param = read(pointer+j*8,'<3hH')
                        aid,rotation = 347,None
                    elif kind == 9:
                        x,y,z,angle,field8,fielda = read(pointer+j*12,'<4h2H')
                        index = fielda&31
                        aid = struct.unpack_from('<H',common,
                            0x0210cb88-common_base+index*2)[0]
                        param = field8 | (common[0x0210cb70-common_base+index]<<16)
                        rotation = [(field8>>8)&15,angle,(field8>>12)&15]
                    else:
                        x,y,z,rx,ry = read(pointer+j*14,'<5h')
                        raw = read(pointer+j*14+10,'<4B')
                        param = int.from_bytes(bytes(raw),'big')
                        aid,rotation = 349,[-rx,-ry,None]
                    row['actors'].append({'record':j,'actor_id':aid,
                        'position_units':[x,y,z],'rotation_units':rotation,
                        'param':param,'profile_candidates':profiles[aid]})
                row['selection_note']='Actor ID comes from original specialized loader, not standard/simple raw-index mapping'
            else:
                row['selection_note']='Nonstandard/simple loader; dynamic spawns require source/runtime review'
            output.append(row)
        return output

    _, global_objects = read(root_address,'<II')
    areas, area_count = read(root_address+16,'<IB')
    tables = table(global_objects,-1)
    for area in range(area_count):
        pointer, = read(areas+area*12,'<I')
        if pointer:
            tables += table(pointer,area)
    by_star = {}
    for star in range(1,8):
        counts = collections.Counter(a['actor_id'] for t in tables if t['star_filter'] in (0,star)
            for a in t['actors'])
        by_star[str(star)] = [{'actor_id':a,'records':n,'profiles':profiles[a]}
            for a,n in sorted(counts.items())]
    levels = collect(root) if numeric_levels is None else numeric_levels
    spans = [(hex(start),hex(end)) for i,start,end,*_ in reset_records(root,levels) if i == ov]
    missing_common = {a['actor_id']:a for rows in by_star.values() for a in rows
        if a['profiles'] and any(p['overlay']==2 and a['actor_id'] not in registered_common for p in a['profiles'])}
    return {'scope':'Offline original-ROM inventory and source-copy preparation, not native PASS',
        'level':level,'level_overlay':ov,'level_root':hex(root_address),'area_count':area_count,
        'object_bank_tags':list(tags),'requested_overlays_original_order':requested,
        'unsupported_overlays':[i for i in requested if support[i]['kind']==0],
        'lifecycle':{str(i):support[i] for i in requested},'numeric_import_spans':spans,
        'tables':tables,'actor_records_by_star_filter':by_star,'overlay_inventory':inventory,
        'loader_handler_symbols':handlers,
        'stage_controller_ids':[
            {'actor_id':332,'profile_symbol':'g_profile_CAMERA','profile_module':'main',
             'profile_address':hex(word(0x02090864+332*4)),
             'condition':'LoadEntranceObjects creates one controller for the selected entrance'},
            {'actor_id':335,'profiles':profiles[335],
             'condition':'LoadClsnAndObjects non-VS branch'},
            {'actor_id':334,'profiles':profiles[334],
             'condition':'LoadClsnAndObjects non-VS branch without intro'}],
        'common_profiles_absent_from_current_manifest':list(missing_common.values()),
        'provenance':{'arm9_sha256':sha(root/'extracted/arm9_dec.bin'),
            'level_overlay_sha256':sha(root/f'extracted/overlays/overlay_{ov:04}.bin'),
            'loader_source_sha256':sha(root/'src/stage/LevelObjects.cpp'),
            'native_info_sha256':sha(native_info)},
        'limits':['Single-player branch only; multiplayer skips object banks in original code',
            'Standard/simple, entrance, teleport-source, door and exit actor records enumerated; actor-created children and delegated-loader internals need further closure review',
            'Entrances are all listed; runtime selects its original subset, not every entry',
            'Exit rotation Z is not assigned by the original loader and is reported as unknown',
            'Original source coverage is not linker, PMF/vtable, resource lifecycle or runtime proof']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('--level',type=int,required=True)
    parser.add_argument('--native-info',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--copy-original-tus',action='store_true')
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    assert not output.exists(), 'Keep previous audit archives immutable'
    result = analyze(root,args.level,args.native_info.resolve())
    output.mkdir(parents=True)
    if args.copy_original_tus:
        copied = {}
        for item in result['overlay_inventory']['overlays']:
            if item['overlay'] not in result['unsupported_overlays']:
                continue
            for name in item['source_translation_units']:
                source = root/name
                assert source.is_file()
                destination = output/'original-tu'/name
                destination.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(source,destination)
                copied[name] = sha(source)
        result['copied_original_tu_sha256'] = copied
    shutil.copy2(Path(__file__),output/Path(__file__).name)
    (output/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Level',args.level,'requested',result['requested_overlays_original_order'],
        'unsupported',result['unsupported_overlays'],'tables',len(result['tables']),
        'original copies',len(result.get('copied_original_tu_sha256',{})))


if __name__ == '__main__':
    main()
