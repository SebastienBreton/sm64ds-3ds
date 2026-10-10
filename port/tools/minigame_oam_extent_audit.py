#!/usr/bin/env python3
"""Review explicit retail OamAttr descriptor roots, including table relocations.
No descriptor type guessed from arbitrary data. Input names are source-audited
OAM consumer targets; each 8-byte stream must end with attr3==0xffff. Report
all interior retail labels and any bytes not retained by the imported closure.
"""
import argparse,bisect,json,re,struct
from pathlib import Path

def audit(root,closure,names):
 if not names:raise ValueError("At least one source-audited OAM consumer is required")
 symbols={}
 for m in re.finditer(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',(root/'config/arm9/overlays/ov006/symbols.txt').read_text(),re.M):
  symbols[int(m[3],16)]=(m[1],m[2])
 relocation_sites=sorted(int(m[1],16) for m in re.finditer(r'from:0x([0-9a-f]+)',(root/'config/arm9/overlays/ov006/relocs.txt').read_text()))
 base=0x020bfec0;blob=(root/'extracted/overlays/overlay_0006.bin').read_bytes();records=closure['symbols'];report=[]
 retained=[(int(v['address'],16),int(v['end'],16)) for v in records.values() if v['module']=='6' and 'end' in v and v['kind']!='bss']
 for name in names:
  r=records[name]
  if r['module']!='6' or r['kind'].startswith('function') or r['kind']=='bss':raise ValueError('Consumer must be initialized overlay6 data: '+name)
  targets=[v['target'] for v in r.get('relocations',[])] or [name]
  for target in targets:
   record=records[target]
   if record['module']!='6' or record['kind'].startswith('function') or record['kind']=='bss':raise ValueError('Descriptor must be initialized overlay6 data: '+target)
   a=int(record['address'],16);entries=[]
   if a%4 or symbols.get(a,(None,))[0]!=target:raise ValueError('Descriptor address disagrees with retail symbol: '+target)
   for i in range(128):
    at=a+i*8
    if not base<=at<=base+len(blob)-8:raise ValueError('OAM stream outside retail overlay')
    # A relocation inside a descriptor cannot be silently treated as sprite bytes.
    relocation=bisect.bisect_left(relocation_sites,at)
    if relocation<len(relocation_sites) and relocation_sites[relocation]<at+8:raise ValueError('OAM stream crossed a retail relocation: '+target)
    attrs=struct.unpack_from('<4H',blob,at-base);entries.append(attrs)
    if attrs[3]==0xffff:break
   else:raise ValueError('OAM stream has no terminator within128 entries: '+target)
   end=a+8*len(entries);missing=[x for x in range(a,end) if not any(b<=x<e for b,e in retained)]
   labels=[name for at,(name,kind) in sorted(symbols.items()) if a<=at<end]
   report.append({'consumer_root':name,'descriptor':target,'start':hex(a),'end':hex(end),'entries':entries,'interior_labels':labels,'missing_bytes':len(missing),'missing_labels':[symbols[x][0] for x in missing if x in symbols]})
 return {'status':'PASS' if not any(v['missing_bytes'] for v in report) else 'MISSING_DESCRIPTOR_EXTENT','scope':'Explicit source-audited overlay6 OAM consumers only; not every arbitrary data symbol','descriptors':report}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('closure',type=Path);p.add_argument('names',type=Path);p.add_argument('output',type=Path);a=p.parse_args();r=audit(a.root,json.loads(a.closure.read_text()),[line.strip() for line in a.names.read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')]);a.output.write_text(json.dumps(r,indent=2)+'\n');print(r['status'],len(r['descriptors']))
 raise SystemExit(0 if r['status']=='PASS' else 1)
