#!/usr/bin/env python3
"""Verify emitted ARM ELF menu storage against ROM bytes and relocation map."""
from pathlib import Path
import json
from elftools.elf.elffile import ELFFile
root=Path(__file__).resolve().parent.parent;out=root/'build3ds/menu-compile'
report=json.loads((out/'menu_storage.json').read_text())
rom=(root/'sm64ds-decomp/extracted/overlays/overlay_0007.bin').read_bytes()
with (out/'objects/menu_storage.o').open('rb') as f:
    elf=ELFFile(f);data=elf.get_section_by_name('.data.sm64ds_menu').data()
    reloc=elf.get_section_by_name('.rel.data.sm64ds_menu');offsets={r['r_offset'] for r in reloc.iter_relocations()}
    assert len(offsets)==report['native_pointer_relocations']
    original=b''.join(rom[a-0x020ad660:b-0x020ad660] for a,b in report['data_ranges'])
    assert len(data)==len(original)
    changed={i for offset in offsets for i in range(offset,offset+4)}
    assert all(a==b for i,(a,b) in enumerate(zip(original,data)) if i not in changed)
    for start,end,_ in report['nitro_resources']:
        offset=start-report['data_ranges'][0][0]
        assert data[offset:offset+end-start]==rom[start-0x020ad660:end-0x020ad660]
        assert not any(offset<=r<offset+end-start for r in offsets)
    assert elf.get_section_by_name('.bss.sm64ds_menu')['sh_size']==0x1900
    assert elf.get_section_by_name('.bss.sm64ds_menu_common')['sh_size']==0xb0
result={'result':'PASS','data_bytes':len(data),'native_pointer_relocations':len(offsets),'nitro_resources_byte_identical':20,'bss_bytes':0x1900,'common_state_bytes':0xb0}
(out/'storage-result.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
