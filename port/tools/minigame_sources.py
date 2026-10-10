#!/usr/bin/env python3
"""Narrow native ABI/MMIO repairs for original minigame source generation."""
from pathlib import Path
import argparse
import re
def discarded_return(text, marker):
    if text.count(marker)!=1:raise ValueError('Discarded-result function changed: '+marker.strip())
    begin=text.index('{',text.index(marker));depth=1;end=begin+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[:end-1]+'    return 0; // discarded native ABI result\n'+text[end-1:]
def adapt(tree):
    for path in tree.rglob('*.cpp'):
        text=path.read_text()
        if path.name.startswith('__sinit_ov006_'):
            # Recovered C declarations f() accept the retail arguments, but
            # become no-argument prototypes when hostgen emits C++. Preserve
            # the evidenced shared-file and exit-chain ABIs in generated code.
            for name in ('func_02017acc', 'func_02017a24', '_ZN13SharedFilePtr9ConstructEj'):
                text=re.sub(r'extern (?:int|void) '+name+r'\(\);',
                            'extern void* '+name+'(void*, unsigned int);',text)
            if 'extern void func_020731dc();' in text:
                text=text.replace('extern void func_020731dc();',
                                  'extern void func_020731dc(void*, void*, void**);')
                text=re.sub(r'func_020731dc\((\w+),\s*(&?\w+),\s*(\w+)\);',
                            lambda m:'func_020731dc('+m[1]+', reinterpret_cast<void*>('+m[2]+'), reinterpret_cast<void**>('+m[3]+'));',text)
        if path.name in ('func_02017a0c.cpp', 'func_02017a9c.cpp'):
            # Shared-model destruction veneers forward their receiver in r0.
            callee='func_02017a9c' if path.name=='func_02017a0c.cpp' else 'func_02017e34'
            before='extern int '+callee+'();'
            if text.count(before)!=1:raise ValueError('Shared model destructor declaration changed')
            text=text.replace(before,'extern int '+callee+'(int);')
        if path.name=='d_s_mg_jump.cpp':
            for name in ('_ZTV14dScMgD3DBase_c', '_ZTV11dScMgJump_c'):
                before='*(int *)p = (int)'+name+';'
                if text.count(before)!=1:raise ValueError('Jump factory vtable changed')
                text=text.replace(before,'*(int *)p = (int)('+name+' + 2);')
        if path.name=='d_s_mg_jump2.cpp':
            before='*(char **)raw = _ZTV14dScMgD3DBase_c;'
            if text.count(before)!=1:raise ValueError('Jump2 base vtable changed')
            text=text.replace(before,'*(char **)raw = _ZTV14dScMgD3DBase_c + 8;')
        if path.name=='func_020126ac.cpp':
            before='extern int func_020127ec();'
            if text.count(before)!=1:raise ValueError('Amida sound wrapper declaration changed')
            text=text.replace(before,'extern void func_020127ec(int, int, int, int, int, int);')
        if path.name=='dScMgAmida_c.cpp':
            before='    LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());'
            if text.count(before)!=1:raise ValueError('Amida BG setup tail changed')
            # ROM020d1214 BL loader, pop/bx preserves its r0 result.
            text=text.replace(before,'    return LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());')
        if path.name=='dScMgSnowball_c.cpp':
            before='*(void **)t = &_ZTV19dScMgSingle3DBase_c;'
            if text.count(before)!=1:raise ValueError('Snowball base vtable changed')
            # Derived [2] already uses slot0; base must skip GCC's prefix too.
            text=text.replace(before,'*(void **)t = (char*)&_ZTV19dScMgSingle3DBase_c + 8;')
        if path.name=='d_s_mg_m_carlo.cpp':
            before='(int)&_ZTV19dScMgSingle3DBase_c;'
            if text.count(before)!=1:raise ValueError('MCarlo base vtable changed')
            # Retail points at slot0; GCC stores its two-word prefix before it.
            # The derived factory already selects native slot0 with [2].
            text=text.replace(before,'(int)((char*)&_ZTV19dScMgSingle3DBase_c + 8);')
        if path.name=='d_s_mg_memory.cpp':
            before='*(void **)scene = _ZTV19dScMgSingle3DBase_c;'
            if text.count(before)!=1:raise ValueError('Memory base vtable changed')
            # The derived Memory factory already uses GCC's +2 prefix.
            text=text.replace(before,'*(void **)scene = (char*)_ZTV19dScMgSingle3DBase_c + 8;')
        if path.name=='d_s_mg_flower.cpp':
            for name in ('_ZTV19dScMgSingle3DBase_c','_ZTV13dScMgFlower_c'):
                before='(int)&'+name+';'
                if text.count(before)!=1:raise ValueError('Flower factory vtable changed')
                text=text.replace(before,'(int)((char*)&'+name+' + 8);')
        if path.name=='d_s_mg_single3_d_base.cpp':
            pattern=r'\*\(vu(16|32) \*\)0x([0-9a-f]+)u'
            if len(re.findall(pattern,text))!=10:raise ValueError('Single3DBase MMIO changed')
            text=re.sub(pattern,lambda m:'NTR_MMIO(u'+m[1]+', 0x'+m[2]+')',text)
        if path.name=='dScMgD3DBase_c.cpp':
            # Retail base BeforeInitResources dispatches slot7C at020b0a10
            # then overwrites r0 with1 at020b0a14. Its result is discarded.
            text=discarded_return(text,'int dScMgD3DBase_c::Virtual7C()\n')
            # Retail020e6e3c tail-jumps to the void sound wrapper02012718.
            # The Jump Mario callers invoke it for sound only; none use r0.
            text=discarded_return(text,'extern "C" int func_ov006_020e6e3c(int a, int b)\n')
        if path.name=='func_02053008.cpp':
            for index in [0,1]:
                expected=f'NTR_MMIO(unsigned int, (0x40002b8 + sizeof(unsigned int) * ({index})))'
                if text.count(expected)!=2:raise ValueError('Shared square-root operand translation changed')
        if path.name=='func_02053130.cpp':
            for before,after in [
                ('volatile u16 *sqrtcnt;','auto sqrtcnt = REG_SQRTCNT;'),
                ('sqrtcnt = &REG_SQRTCNT;','// Register proxy retains the original polling reads.'),
                ('while (*sqrtcnt & SQRT_CONTROL_BUSY)','while (sqrtcnt & SQRT_CONTROL_BUSY)')]:
                if text.count(before)!=1:raise ValueError('Reciprocal square-root polling changed')
                text=text.replace(before,after)
        if path.name=='d_s_mg_base.cpp':
            before='extern int data_ov004_020bc03c[];'
            if text.count(before)!=1:raise ValueError('Minigame graph callback declaration changed')
            text=text.replace(before,'extern int _ZTVN11dScMgBase_c15graphCallback_cE[];')
            before='*(int*)c = (int)data_ov004_020bc03c;'
            if text.count(before)!=1:raise ValueError('Minigame graph callback constructor changed')
            # Retail labels point at the first virtual function. Native GCC
            # emits the offset-to-top and typeinfo prefix before that slot.
            text=text.replace(before,'*(int*)c = (int)(_ZTVN11dScMgBase_c15graphCallback_cE + 2);')
            before='    LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());'
            if text.count(before)!=1:raise ValueError('Minigame BG setup tail changed')
            # Retail 020b28f4 BL LoadCompressedFileAt, then pop/bx lr: r0
            # remains the loader result. Falling out of an int C++ method is UB.
            text=text.replace(before,'    return LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());')
            for method in ('OnHitFromUnderneath', 'OnAimedAtWithEgg'):
                marker=f'int dScMgBase_c::{method}()\n'
                # Both virtuals are used for their side effects; the retail
                # callers discard r0. GCC must still receive a defined C++
                # return to emit an epilogue rather than enter the literal pool.
                text=discarded_return(text,marker)
        if path.name=='dScMgLuigi_c.cpp':
            before='    volatile unsigned short *ime = &IME;'
            if text.count(before)!=1:raise ValueError('Wanted IME local changed')
            # Retain the original read/disable/read/restore sequence with a
            # register proxy, whose address is not a hardware-memory pointer.
            text=text.replace(before,'    auto ime = IME;')
            text=text.replace('saved = *ime;','saved = ime;').replace('*ime =','ime =')
            text=text.replace('{ *ime;','{ (void)static_cast<unsigned short>(ime);')
        if path.name=='dScMgRoulette_c.cpp':
            # Retail02109798: ldrsh r0,[this,#mScore]; bl func_ov004_020b56c8
            # (payout coin rain count). The recovered void prototype dropped
            # the argument; natively r0 was garbage and the coin rain never ended.
            before='void func_ov004_020b56c8(void);'
            if text.count(before)!=1:raise ValueError('Roulette payout declaration changed')
            text=text.replace(before,'void func_ov004_020b56c8(int);')
            before='func_ov004_020b56c8();'
            if text.count(before)!=1:raise ValueError('Roulette payout call changed')
            text=text.replace(before,'func_ov004_020b56c8(mScore);')
        if path.name=='func_02012dd0.cpp':
            before='extern int func_0204f9c4();'
            if text.count(before)!=1:raise ValueError('Sound-player parameter declaration changed')
            text=text.replace(before,'extern void func_0204f9c4(int, int);')
            for index in ['0','1','0xe','0x15','0x1f']:
                before=f'func_0204f9c4({index}, c);'
                if text.count(before)!=1:raise ValueError('Sound-player call changed')
                text=text.replace(before,f'func_0204f9c4({index}, (int)c);')
        # Any remaining bare primary-vptr store of a GCC vtable symbol (retail
        # labels name the first virtual slot; GCC emits offset-to-top+typeinfo
        # before it). Found by admitting Roulette, Slot1 and 3DEsp: their
        # factories stored the table start, so virtual calls read typeinfo.
        text=BARE_VPTR_STORE.sub(lambda m:m[1]+(m[2] or '(void*)')+'((char*)&'+m[4]+' + 8)'+m[5],text)
        path.write_text(text)
BARE_VPTR_STORE=re.compile(r'(=\s*)(\(\s*[A-Za-z_][\w\s]*\**\s*\)\s*)?(&\s*)?(_ZTV\w+)(\s*;)')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('tree',type=Path)
    adapt(p.parse_args().tree)
