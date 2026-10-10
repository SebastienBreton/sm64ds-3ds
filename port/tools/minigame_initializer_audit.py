"""Source + ROM constructor provenance for retained BSS roots, never a linker-closure claim."""
from pathlib import Path
import argparse,json,re,struct

def audit(root,manifest,original_sources):
 cfg=root/'config/arm9/overlays/ov006';blob=(root/'extracted/overlays/overlay_0006.bin').read_bytes();base=0x020bfec0
 syms={n:(k,int(a,16)) for n,k,a in re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',(cfg/'symbols.txt').read_text(),re.M)}
 match=re.search(r'\.ctor\s+start:0x([0-9a-f]+) end:0x([0-9a-f]+)',(cfg/'delinks.txt').read_text());start,end=[int(x,16) for x in match.groups()];ctors=list(struct.unpack_from('<'+str((end-start)//4)+'I',blob,start-base));assert struct.unpack_from('<I',blob,end-base)[0]==0
 imported=set(re.findall(r'SM64DS_MINIGAME_INIT\((\w+),',(Path(__file__).resolve().parents[1] / 'menu/minigame_imports.inc').read_text()));rows=[]
 roots=[n for n in manifest['roots'] if manifest['symbols'][n]['kind']=='bss' and manifest['symbols'][n]['module']=='6']
 source_text='\n'.join(Path(x).read_text() for x in original_sources)
 initializers=[]
 for f in sorted((root/'src').glob('__sinit_ov006_*')):
  text=f.read_text(); name=f.stem;address=syms.get(name,('',None))[1]
  if address not in ctors:continue
  initializers.append((name,address,ctors.index(address),f,text))
 for name in sorted(roots):
  writers=[];pattern=r'\b'+re.escape(name)+r'(?:\s*(?:\[[^\]]*\]|\.\w+))*\s*(?:=(?!=)|\|=|&=|\+=|-=)'
  for fn,at,index,f,text in initializers:
   if not re.search(r'\b'+re.escape(name)+r'\b',text):continue
   assignments=[line.strip() for line in text.splitlines() if re.search(pattern,line)]
   writers.append({'initializer':fn,'address':hex(at),'retail_order':index,'already_imported':fn in imported,'source':str(f),'direct_assignments':assignments,'classification':'direct_writer' if assignments else 'referenced_indirect_effect_requires_review'})
  lazy=bool(re.search(r'if\s*\([^\n]*\b'+re.escape(name)+r'\b',source_text) and re.search(pattern,source_text))
  rows.append({'root':name,'address':manifest['symbols'][name]['address'],'constructor_writers':writers,'source_lazy_guard_observed':lazy,'classification':'retail_ctor_required' if writers else 'lazy_guard' if lazy else 'no_direct_ctor_writer_found_review_runtime_writes','note':'No direct writer found is not proof of zero-only or initialized data; follow aliases and helper calls.'})
 required=sorted({w['initializer'] for row in rows for w in row['constructor_writers'] if not w['already_imported']})
 return {'status':'SOURCE_ROM_CTOR_PROVENANCE_REVIEW_REQUIRED','BSS_roots':rows,'additional_constructor_candidates':required,'scope':'Retained BSS roots only; no automatic whole-overlay import. Link symbol availability cannot prove initialization.'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('manifest',type=Path);p.add_argument('output',type=Path);p.add_argument('sources',nargs='+',type=Path);a=p.parse_args();r=audit(a.root,json.loads(a.manifest.read_text()),a.sources);a.output.write_text(json.dumps(r,indent=2)+'\n');print(r['additional_constructor_candidates'])
