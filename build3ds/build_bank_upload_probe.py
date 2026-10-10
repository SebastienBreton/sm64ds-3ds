#!/usr/bin/env python3
"""Generate native fixtures from frozen independent retail ARM transactions."""
from pathlib import Path
import hashlib,json,shutil,subprocess
root=Path(__file__).resolve().parent.parent
out=root/'build3ds/native-bank-upload';out.mkdir(exist_ok=True)
golden=root/'build3ds/reference-menu/bank-upload/arm-golden.json'
golden_bytes=golden.read_bytes()
golden=out/'arm-golden.json';golden.write_bytes(golden_bytes)
data=json.loads(golden_bytes)
operations=list(data['entries'])
assert operations==['_ZN2GX13SetBankForTexEt','_ZN2GX17SetBankForTexPlttEt','InitialiseVramGlobals','_ZN2GX12BeginLoadTexEv','_ZN2GX10EndLoadTexEv','_ZN2GX16BeginLoadTexPlttEv','_ZN2GX14EndLoadTexPlttEv','_ZN2GX7LoadTexEPKvjj','_ZN2GX11LoadTexPlttEPKvjj']
cases=[];steps=[];writes=[]
def array(values):return '{'+','.join(hex(v) for v in values)+'}'
for case in data['cases']:
    cases.append('{'+','.join(map(str,[case['texture_mask'],case['palette_mask'],case['dma_channel'],len(steps),len(case['stages'])]))+'}')
    for stage in case['stages']:
        transactions=[t for t in stage['transactions'] if 0x06800000<=t[0]<0x068a4000]
        first=len(writes);writes.extend('{'+','.join(map(str,t))+'}' for t in transactions)
        args=stage['arguments'];op=operations.index(stage['operation'])
        steps.append('{'+','.join([str(op),str(args[1] if op in (7,8) else 0),str(args[2] if op in (7,8) else 0),str(first),str(len(transactions)),array(stage['state_halfwords'][:16]),array(stage['windows_words']),array(stage['budget_words']),str(stage['io60']),array(stage['vramcnt_bytes'])])+'}')
header='''#pragma once
struct UploadWrite {unsigned address,width,value;};
struct UploadStep {unsigned op,offset,bytes,first_write,write_count;unsigned short state[16];unsigned windows[6],budget[9];unsigned short io60;unsigned char controls[10];};
struct UploadCase {unsigned tex,pal;int dma;unsigned first,count;};
'''
for name,kind,items in [('upload_writes','UploadWrite',writes),('upload_steps','UploadStep',steps),('upload_cases','UploadCase',cases)]:header+=f'static const {kind} {name}[]={{\n'+',\n'.join(items)+'\n};\n'
(out/'bank_upload_golden.h').write_text(header)
source=out/'smoke_gx_bank_upload.cpp';shutil.copy2(root/'port/tests/smoke_gx_bank_upload.cpp',source)
subprocess.run([str(root/'.venv/bin/python'),str(root/'build3ds/build_menu_probe.py'),str(source)],check=True)
for suffix in ('elf','3dsx'):shutil.copy2(root/f'build3ds/menu-compile/{source.stem}.{suffix}',out/f'{source.stem}.{suffix}')
report={'status':'LINKED_NOT_RUN','cases':len(cases),'snapshots':len(steps),'cpu_uploads':sum(s['operation'] in operations[7:] for c in data['cases'] for s in c['stages']),'scope':'real native SDK, full physical banks for CPU-upload cases; DMA payload excluded','sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [golden,out/'bank_upload_golden.h',source,out/f'{source.stem}.3dsx',root/'port/platform/3ds/gx_upload_physical.cpp']}}
(out/'build-report.json').write_text(json.dumps(report,indent=2)+'\n');print(report['status'],report['cases'],report['snapshots'])
