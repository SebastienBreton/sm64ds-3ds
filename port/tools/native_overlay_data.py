"""Common ROM-checked import for reviewed native overlay data manifests.

Fail closed on unknown edges, inconsistent profiles or nonzero-adjustment PMFs.
Only reviewed data islands and BSS are emitted; compiler owns RTTI/vtables.
"""
import re,struct

def emit_bss(ov,labels,start,end,always_gap=False):
    out=[f'.pushsection .bss.overlay{ov},"aw",%nobits','.balign 4',
         f'.global sm64ds_overlay{ov}_bss_begin',f'sm64ds_overlay{ov}_bss_begin:']
    cursor=start
    for address in sorted(labels):
        if not start<=address<end:continue
        if always_gap or address>cursor:out.append(f'.zero {address-cursor}')
        for name in labels[address]:out += [f'.global {name}',name+':']
        cursor=address
    out += [f'.zero {end-cursor}',f'.global sm64ds_overlay{ov}_bss_end',
            f'sm64ds_overlay{ov}_bss_end:','.popsection']
    return out

def generate(root, ov, base, spans, bss_start, bss_end, profile_count, pmf_spans, extra_lines=(), reviewed_external_edges=None):
    cfg=root/f'config/arm9/overlays/ov{ov:03}'
    blob=(root/f'extracted/overlays/overlay_{ov:04}.bin').read_bytes()
    if base+len(blob)!=bss_start:raise ValueError('native overlay image size changed')
    labels={};kinds={}
    for n,k,a in re.findall(r'^(\w+) kind:(\S+) addr:0x([0-9a-f]+)',(cfg/'symbols.txt').read_text(),re.M):
        at=int(a,16);labels.setdefault(at,[]).append(n);kinds[n]=k
    edges={};reviewed_external_edges=reviewed_external_edges or {};seen_external=set()
    for a,t,mod in re.findall(r'^from:0x([0-9a-f]+) kind:load to:0x([0-9a-f]+) module:(\S+)',(cfg/'relocs.txt').read_text(),re.M):
        a,t=int(a,16),int(t,16)
        if any(lo<=a<hi for lo,hi in spans):
            if a in reviewed_external_edges:
                expected_target,expected_module,name=reviewed_external_edges[a]
                if (t,mod)!=(expected_target,expected_module) or struct.unpack_from('<I',blob,a-base)[0]!=t:
                    raise ValueError(f'reviewed external pointer changed {a:x}')
                edges[a]=name;seen_external.add(a);continue
            if mod!=f'overlay({ov})' or t not in labels or struct.unpack_from('<I',blob,a-base)[0]!=t:raise ValueError(f'unreviewed pointer {a:x}')
            n=labels[t][0]
            if not (kinds[n].startswith('function') or bss_start<=t<bss_end or any(lo<=t<hi for lo,hi in spans)):raise ValueError(f'pointer outside imports {a:x}->{t:x}')
            edges[a]=n
    if seen_external!=set(reviewed_external_edges):raise ValueError('unused reviewed external edge')
    arm=(root/'extracted/arm9_dec.bin').read_bytes();profiles=[]
    for a,names in labels.items():
        for n in names:
            if n.startswith('g_profile_'):
                target,prior=struct.unpack_from('<II',blob,a-base);actor=prior&65535
                if struct.unpack_from('<I',arm,0x2090864-0x2004000+actor*4)[0]!=a or target not in labels:raise ValueError('profile registry mismatch')
                profiles.append((actor,n))
    if len(profiles)!=profile_count:raise ValueError('native overlay profile count changed')
    # Explicit native nonvirtual PMFs, unchanged two-word representation.
    for a in [at for lo,hi in pmf_spans for at in range(lo,hi,8)]:
        fn,adjust=struct.unpack_from('<II',blob,a-base)
        if a not in edges or adjust or not kinds[labels[fn][0]].startswith('function'):raise ValueError('PMF format changed')
    out=['// Generated native pointers; no DS vtables or executable bytes.', 'asm(R"OV_NATIVE(']
    for lo,hi in spans:
        out += [f'.pushsection .data.overlay_native_{lo:x},"aw",%progbits','.balign 4']
        for a in range(lo,hi,4):
            for n in labels.get(a,[]):out += [f'.global {n}',n+':']
            out.append('.word '+edges.get(a,f'0x{struct.unpack_from("<I",blob,a-base)[0]:08x}'))
        out+=['.popsection']
    out += emit_bss(ov,labels,bss_start,bss_end,always_gap=True)
    out += [')OV_NATIVE");']
    out += extra_lines
    out += ['extern "C" {']+[f'extern unsigned char {n}[];' for _,n in profiles]
    out += ['unsigned sm64ds_overlay_native_ids[]={'+','.join(str(i) for i,_ in profiles)+'};',
            'void* sm64ds_overlay_native_profiles[]={'+','.join(n for _,n in profiles)+'};','}']
    return ('\n'.join(out)+'\n').replace('overlay_native',f'overlay{ov}').replace('OV_NATIVE',f'OV{ov}')
