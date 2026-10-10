#!/usr/bin/env python3
"""Inventory overlay62 symbols against player objects (not runtime validation)."""
from pathlib import Path
import subprocess,shlex,json
root=Path(__file__).resolve().parent.parent;build=root/'build3ds/cmake'
nm='/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
new=list((build/'CMakeFiles/sm64ds_overlay62.dir').rglob('*.o'))
existing=[build/p for p in shlex.split((build/'CMakeFiles/smoke_player.dir/link.txt').read_text()) if p.endswith('.o')]+[build/'libntr.a',build/'libsm64ds_sseq.a']
def symbols(paths):
 out=subprocess.check_output([nm,'-g',*map(str,paths)],text=True,stderr=subprocess.DEVNULL)
 defined=set();undefined=set()
 for line in out.splitlines():
  parts=line.split()
  if len(parts)==2 and parts[0]=='U':undefined.add(parts[1])
  elif len(parts)==3 and parts[1] not in ('U','w','v'):defined.add(parts[2])
 return defined,undefined
nd,nu=symbols(new);ed,eu=symbols(existing)
report={'objects':len(new),'definitions':len(nd),'unresolved_against_player_objects':sorted(nu-nd-ed),'note':'Object-level inventory; SDK/archive resolution and native ABI still require validation.'}
p=root/'build3ds/validation/overlay62-compile';p.mkdir(parents=True,exist_ok=True)
(p/'dependencies.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
