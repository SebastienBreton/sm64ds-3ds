#!/usr/bin/env python3
"""Original ARM dispatch/ref inventory and own-return disassembly, not a CFA proof.

Every other site and non-adjacent/computed dispatch remains explicit review.
No result becomes 'discarded' simply because an analogous class discards it.
"""
import argparse,bisect,hashlib,json,re,struct,subprocess
from pathlib import Path


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def inspect(root,out):
    modules=[('main',root/'config/arm9',root/'extracted/arm9_dec.bin',0x02004000)]
    for cfg in sorted((root/'config/arm9/overlays').glob('ov*')):
        de=(cfg/'delinks.txt').read_text();m=re.search(r'^\s+\.text\s+start:0x([0-9a-f]+)',de,re.M)
        blob=root/f'extracted/overlays/overlay_{int(cfg.name[2:]):04}.bin'
        if m and blob.exists():modules.append((cfg.name,cfg,blob,int(m[1],16)))
    sites=[];references=[];input_hashes={};functions={};own_tails=[];rejected=[]
    targets={0x0210c4b8:'Slots slot28',0x02118ae4:'Smartball slot31',0x020aeb24:'slot28 tail dispatcher'}
    for module,cfg,path,base in modules:
        blob=path.read_bytes();input_hashes[str(path)]=sha(path)
        funcs=[]
        for n,arch,size,addr in re.findall(r'^(\w+) kind:function\((\w+),size=0x([0-9a-f]+)\) addr:0x([0-9a-f]+)',(cfg/'symbols.txt').read_text(),re.M):
            a=int(addr,16);size=int(size,16)
            if size:funcs.append((a,a+size,n,arch))
        funcs.sort();starts=[row[0] for row in funcs]
        for start,end,name,arch in funcs:
            if arch!='arm' or not base<=start<end<=base+len(blob):continue
            words=[struct.unpack_from('<I',blob,a-base)[0] for a in range(start,end-3,4)]
            for index,word in enumerate(words):
                # Immediate word LDR, pre-indexed +offset, no writeback. Both
                # AL and conditional loads remain candidates for review.
                if word&0x0ff00000!=0x05900000 or word&0xfff not in (0x70,0x7c):continue
                reg=(word>>12)&15;rn=(word>>16)&15
                if rn==15:
                    rejected.append({'module':module,'owner':name,'load':hex(start+index*4),
                                     'reason':'PC-relative literal load, not a vtable slot load'})
                    continue
                for delta in range(1,min(9,len(words)-index)):
                    candidate=words[index+delta]
                    blx=candidate&0x0ffffff0==0x012fff30 and candidate&15==reg
                    bx=candidate&0x0ffffff0==0x012fff10 and candidate&15==reg
                    if blx or bx:
                        a=start+index*4
                        sites.append({'module':module,'owner':name,'slot':(word&0xfff)//4,
                            'load':hex(a),'call':hex(a+delta*4),'call_kind':'BLX' if blx else 'BX',
                            'loaded_register':reg,'table_register':rn,'instructions_between':delta-1,
                            'raw_words':[hex(w) for w in words[index:min(len(words),index+delta+5)]],
                            'classification':'IN_FAMILY_REVIEW' if module in ('ov004','ov006') else 'OTHER_FAMILY_OR_GENERIC_REVIEW'})
                        break
                    # A following load/data-processing write supersedes the
                    # loaded target; a BL supersedes LR and caller-saved regs.
                    load_write=(candidate&0x0c100000==0x04100000 and (candidate>>12)&15==reg)
                    opcode=(candidate>>21)&15
                    arithmetic_write=(candidate&0x0c000000==0 and opcode not in (8,9,10,11)
                                      and (candidate>>12)&15==reg)
                    branch_link=(candidate&0x0f000000==0x0b000000 and reg in (0,1,2,3,12,14))
                    if load_write or arithmetic_write or branch_link:
                        rejected.append({'module':module,'owner':name,'load':hex(start+index*4),
                                         'reason':'Loaded register overwritten before candidate branch',
                                         'overwrite':hex(start+(index+delta)*4),'word':hex(candidate)})
                        break
            if start in targets and module=='ov006':
                command=['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump','-D','-b','binary','-m','arm',
                         '--adjust-vma='+hex(base),'--start-address='+hex(start),'--stop-address='+hex(end),str(path)]
                run=subprocess.run(['rtk','proxy',*command],capture_output=True,text=True,check=True)
                own_tails.append({'module':module,'symbol':name,'address':hex(start),'bytes':end-start,
                                  'raw_hex':blob[start-base:end-base].hex(),'disassembly':run.stdout,
                                  'required_gate':'Trace own final call result through epilogue; identify every r0 read/write, not an analogy.'})
        for a,kind,target,hint in re.findall(r'from:0x([0-9a-f]+) kind:(\w+) to:0x([0-9a-f]+) module:(\S+)',(cfg/'relocs.txt').read_text()):
            target=int(target,16)
            if target not in targets:continue
            source=int(a,16);index=bisect.bisect_right(starts,source)-1
            owner=funcs[index][2] if index>=0 and source<funcs[index][1] else None
            references.append({'module':module,'from':hex(source),'kind':kind,'target':hex(target),
                               'hint':hint,'owner':owner,'target_identity':targets[target]})
    report={'status':'ORIGINAL_DISPATCH_AND_RETURN_REVIEW_REQUIRED','sites':sites,'references':references,
        'rejected_lookalikes':rejected,
        'own_method_disassembly':own_tails,'input_sha256':input_hashes,
        'limits':['This scanner inventories immediate ARM slot loads followed within8words by BLX/BX of that register.',
                  'Computed-index loads, register copies, Thumb call paths and possible register clobbers require source/caller review; no universal absence claim.',
                  'Other-family sites are retained for explicit classification; merely filtering to ov4/6 is not proof.',
                  'Slot28 tail-dispatcher020aeb24 returns r0 transitively: inspect its sole original caller020b070c, not just the two tail sites.',
                  'No runtime or actor admission authorized by this report alone.']}
    out.write_text(json.dumps(report,indent=2)+'\n');print(len(sites),'dispatch candidates retained; own-return review required')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    inspect(a.root.resolve(),a.output.resolve())
