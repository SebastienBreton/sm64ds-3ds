#!/usr/bin/env python3
"""Prove initialized native data against retail before preferring ROM storage."""
import hashlib,json,re,struct
from pathlib import Path
def elf(path):
    blob=path.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',blob)
    if blob[:5]!=b'\x7fELF\x01' or blob[5]!=1 or h[2]!=40:raise ValueError('Expected ARM ELF32 little endian')
    sections=[struct.unpack_from('<IIIIIIIIII',blob,h[6]+i*h[11]) for i in range(h[12])]
    names=sections[h[13]];strings=blob[names[4]:names[4]+names[5]]
    def name(table,at):return table[at:table.index(b'\0',at)].decode()
    result=[]
    for sec in sections:result.append({'name':name(strings,sec[0]),'type':sec[1],'address':sec[3],'bytes':blob[sec[4]:sec[4]+sec[5]],'link':sec[6],'info':sec[7],'size':sec[5]})
    symtab=next(i for i,s in enumerate(result) if s['type']==2);syms=[]
    symbol_strings=result[result[symtab]['link']]['bytes']
    data=result[symtab]['bytes']
    for at in range(0,len(data),16):
        n,v,size,info,other,section=struct.unpack_from('<IIIBBH',data,at)
        syms.append({'name':name(symbol_strings,n),'offset':v,'size':size,'info':info,'section':section})
    relocs=[]
    for sec in result:
        if sec['type']!=9:continue
        for at in range(0,len(sec['bytes']),8):
            off,info=struct.unpack_from('<II',sec['bytes'],at)
            relocs.append({'section':sec['info'],'offset':off,'type':info&255,'target':syms[info>>8]['name']})
    return result,syms,relocs
def prove(root,audit,obj,names):
    sections,syms,relocs=elf(obj);records=audit['symbols'];blob=(root/'extracted/overlays/overlay_0006.bin').read_bytes();proof=[]
    for name in names:
        entries=[s for s in syms if s['name']==name and s['section'] not in (0,0xfff1)]
        if len(entries)!=1:raise ValueError('Expected one source data definition: '+name)
        symbol=entries[0];record=records[name]
        if record['module']!='6' or symbol['size']<=0 or symbol['size']>record['bytes']:
            raise ValueError('Data initialized extent is not an audited ov6 prefix: '+name)
        offset=symbol['offset'];size=symbol['size'];sec=sections[symbol['section']]
        if sec['type']!=1:raise ValueError('Expected initialized PROGBITS, not lazy/BSS')
        raw=bytearray(sec['bytes'][offset:offset+size]);used=[]
        for reloc in relocs:
            if reloc['section']!=symbol['section'] or not offset<=reloc['offset']<offset+size:continue
            if reloc['type']!=2 or reloc['target'] not in records:
                raise ValueError('Unsupported initialized-data relocation: '+str(reloc))
            index=reloc['offset']-offset;target=records[reloc['target']]
            addend=struct.unpack_from('<I',raw,index)[0]
            value=(int(target['address'],16)+addend)&0xffffffff
            struct.pack_into('<I',raw,index,value);used.append(reloc|{'addend':addend,'retail_value':hex(value)})
        original=int(record['address'],16)-0x020bfec0;retail=blob[original:original+size]
        if raw!=retail:raise ValueError('Native initialized data differs from retail: '+name+' '+raw.hex()+' != '+retail.hex())
        proof.append({'symbol':name,'bytes':size,'retail_address':record['address'],
                      'sha256':hashlib.sha256(raw).hexdigest(),'relocations':used})
    return {'status':'PASS_NATIVE_INITIALIZERS_RELOCATED_TO_RETAIL_ADDRESSES','object':str(obj),
        'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'rows':proof,
        'contract':'Only these proven initialized definitions may become extern declarations; ROM contiguous storage remains canonical.'}
def extern_definitions(text,names):
    edits=[]
    for name in names:
        # Explicit standalone global definitions; reject functions, constructors
        # and any shape that cannot preserve its exact declared type/array.
        pattern=re.compile(r'(?m)^([^\n;{}]*\b'+re.escape(name)+r'(?:\[[^\]]*\])?)\s*=')
        matches=[m for m in pattern.finditer(text)
                 if re.fullmatch(r'[A-Za-z_]\w*(?:::\w+)*\s*[\s*]*',m[1].split(name,1)[0].strip())]
        if len(matches)!=1:raise ValueError('Initialized declaration shape is ambiguous: '+name)
        m=matches[0];end=m.end();depth=0
        while end<len(text):
            c=text[end]
            if c in '{([':depth+=1
            elif c in '})]':depth-=1
            elif c==';' and depth==0:break
            end+=1
        if end==len(text):raise ValueError('Missing declaration terminator')
        declared=m[1].strip()
        if declared.startswith(('extern','static','const')):raise ValueError('Unsupported storage ownership declaration')
        edits.append((m.start(),end+1,'extern '+declared+';'))
    for begin,end,new in sorted(edits,reverse=True):text=text[:begin]+new+text[end:]
    return text

def prepare_ownership(root,audit,requests,out):
    """Stage explicit source-owner requests only after all retail proofs pass.

    This never edits the input source or compiles anything. Requests must name
    the object, generated source and exact duplicate symbols discovered by the
    link planner. A failed proof prevents every staged source from being written.
    """
    prepared=[]
    if not requests:raise ValueError('Empty ownership request')
    seen=set()
    for request in requests:
        obj=Path(request['object']);source=Path(request['source'])
        names=request['symbols']
        if not names or len(names)!=len(set(names)):
            raise ValueError('Empty or duplicate symbol request')
        overlap=seen.intersection(names)
        if overlap:raise ValueError('Multiple requested source owners: '+str(sorted(overlap)))
        seen.update(names)
        proof=prove(root,audit,obj,names)
        text=source.read_text();candidate=extern_definitions(text,names)
        destination=out/(source.stem+'.storage-canonical'+source.suffix)
        prepared.append((destination,candidate,proof|{
            'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'candidate':str(destination),'candidate_sha256':hashlib.sha256(candidate.encode()).hexdigest()}))
    if len({str(p[0]) for p in prepared})!=len(prepared):
        raise ValueError('Candidate source names collide')
    out.mkdir(parents=True,exist_ok=True)
    for destination,candidate,proof in prepared:
        if destination.exists():raise ValueError('Candidate output already exists: '+str(destination))
    for destination,candidate,proof in prepared:destination.write_text(candidate)
    report={'status':'PASS_PRIVATE_INITIALIZED_DATA_OWNERSHIP_PREPARED',
            'requests':[p[2] for p in prepared],'input_sources_modified':False,
            'limits':['Compile and strong-definition closure gates still required',
                      'Only explicit proven initialized data; lazy/BSS is rejected']}
    (out/'ownership-proof.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path)
    parser.add_argument('storage_audit',type=Path)
    parser.add_argument('requests',type=Path)
    parser.add_argument('out',type=Path)
    args=parser.parse_args()
    report=prepare_ownership(args.root,json.loads(args.storage_audit.read_text()),
                             json.loads(args.requests.read_text())['requests'],args.out)
    print(report['status'])
