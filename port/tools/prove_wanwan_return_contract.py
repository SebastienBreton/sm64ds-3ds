#!/usr/bin/env python3
"""Independent ROM/callsite proof for private Wanwan void-return adaptation.

This proves return observability and sound input ABI only, not full actor state.
"""
import argparse,hashlib,json,re,struct,subprocess
from pathlib import Path


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();root=args.root.resolve();out=args.output.resolve()
    assert not out.exists();out.mkdir(parents=True)
    ov=root/'extracted/overlays/overlay_0014.bin';arm=root/'extracted/arm9_dec.bin'
    blob=ov.read_bytes();main=arm.read_bytes()
    word=lambda a:struct.unpack_from('<I',blob,a-0x021111a0)[0]
    assert word(0x02112a28)==0xe3a00001 # landing result immediately overwritten
    assert word(0x02112a60)==0xe1a00004 # state-reader result immediately overwritten
    assert [word(a) for a in range(0x02111f40,0x02111f50,4)]==[
        0xe12fff31,0xe28dd004,0xe8bd4000,0xe12fff1e] # call then epilogue only
    assert struct.unpack_from('<6I',main,0x0201267c-0x02004000)==(
        0xe59fc00c,0xe1a02001,0xe1a01000,0xe3a00003,0xe12fff1c,0x02012590)
    targets={0x02111fb8,0x02111af0,0x0211150c,0x02111f08}
    inbound=[];files=[ov,arm,root/'src/game/actors/d_a_wanwan.cpp',root/'src/func_0201267c.cpp']
    for config in (root/'config').rglob('relocs.txt'):
        for line in config.read_text().splitlines():
            match=re.fullmatch(r'from:0x([0-9a-f]+) kind:(\S+) to:0x([0-9a-f]+) module:overlay\(14\)',line)
            if match and int(match[3],16) in targets:
                assert config==root/'config/arm9/overlays/ov014/relocs.txt'
                inbound.append((int(match[1],16),match[2],int(match[3],16)))
                files.append(config)
    assert set(inbound)=={(0x02112a24,'arm_call',0x02111fb8),
        (0x02112a5c,'arm_call',0x02111f08),(0x021146a0,'load',0x02111af0),
        (0x021146c8,'load',0x0211150c)}
    code=files[2].read_text()
    assert 'typedef void (WanwanState::*WanwanPmf)();' in code
    assert '(c->*data_ov014_0211476c[j].pmf)();' in code
    assert struct.unpack_from('<2I',blob,0x021146a0-0x021111a0)==(0x02111af0,0)
    assert struct.unpack_from('<2I',blob,0x021146c8-0x021111a0)==(0x0211150c,0)
    commands=[]
    for name,image,base,lo,hi in (
        ('behavior',ov,0x021111a0,0x021129ec,0x02112a78),
        ('state-reader',ov,0x021111a0,0x02111f08,0x02111f54),
        ('sound-inputs',arm,0x02004000,0x0201267c,0x02012694)):
        cmd=['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump','-D','-b','binary','-m','arm',
            '--adjust-vma='+hex(base),'--start-address='+hex(lo),'--stop-address='+hex(hi),str(image)]
        p=subprocess.run(cmd,capture_output=True,text=True,check=True);commands.append(cmd)
        (out/(name+'.asm')).write_text(p.stdout)
    result={'status':'PASS exact ROM ignored-result contract and two-argument sound ABI',
        'inbound':inbound,'original_sha256':{str(p):sha(p) for p in set(files)},'commands':commands,
        'evidence':['Landing sole caller overwrites r0 before use',
            'Two state targets only reside in zero-adjustment void PMFs',
            'PMF reader epilogue observes no result; sole caller overwrites r0',
            'Sound wrapper overwrites incoming r2 from r1 before tail call'],
        'limits':['Does not prove actor field layout, remaining helpers or gameplay',
            'No original src/include mutation; adapted copy still needs compilation and runtime gates']}
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(result['status'])


if __name__=='__main__':main()
