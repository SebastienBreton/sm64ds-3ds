"""Private, evidenced ARM32 adaptations; never alter reconstructed originals.

Canonical C prototypes come from the original function definition. Pointer/int
arguments retain their 32 bits; no guessed arity, variadic wrapper or fallback.
Unknown result use or non-register argument shape is a blocker.
"""
import hashlib
import re


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def masked_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*',lambda m:re.sub(r'[^\n]',' ',m[0]),text,flags=re.S)


def arguments(text):
    result=[];start=0;depth=0
    for index,char in enumerate(text):
        if char in '([{':depth+=1
        elif char in ')]}':depth-=1
        elif char==',' and depth==0:result.append(text[start:index].strip());start=index+1
    if text[start:].strip():result.append(text[start:].strip())
    return result


def closing(text,opening):
    depth=1;index=opening+1
    while index<len(text) and depth:
        depth+=(text[index]=='(')-(text[index]==')');index+=1
    if depth:raise ValueError('Unbalanced native call arguments')
    return index-1


def canonical_functions(original):
    clean=re.sub(r'/\*.*?\*/|//[^\n]*','',original,flags=re.S)
    result={}
    for m in re.finditer(r'\b(void|int|char\s*\*)\s+(func_\w+|RenderOamMainScreen)\s*\(([^()]*)\)\s*\{',clean):
        result[m[2]]={'result':m[1],'parameters':m[3], 'definition':m[0][:-1].strip(),
                     'original_definition_sha256':sha(m[0])}
    return result


def canonicalize(text,name,signature,ledger):
    declaration=re.compile(r'(?P<prefix>\b(?:extern\s+(?:"C"\s+)?)?)'
        r'(?P<result>void|int|char\s*\*)\s+'+re.escape(name)+r'\s*\((?P<args>[^();]*)\)\s*;')
    changes=[]
    def replace(m):
        params=arguments(m['args']);expected=arguments(signature['parameters'])
        if len(params)!=len(expected) and m['args'].strip() not in ('','void'):
            raise ValueError('C arity conflict requires review: '+name)
        changes.append({'before':m[0], 'after':m['prefix']+signature['definition']+';',
                        'return_mismatch':m['result'].strip()!=signature['result'].strip()})
        return m['prefix']+signature['definition']+';'
    text=declaration.sub(replace,text)
    if not changes:return text
    types=[]
    for parameter in arguments(signature['parameters']):
        if parameter.strip()=='void':continue
        m=re.match(r'^(.*?)\b\w+\s*$',parameter.strip())
        if not m:raise ValueError('Unparsed canonical parameter: '+parameter)
        kind=m[1].strip()
        if '*' not in kind and kind not in ('int','unsigned int','u32','s32'):
            raise ValueError('Non-ARM32-register parameter requires review: '+parameter)
        types.append(kind)
    edits=[]
    masked=masked_comments(text)
    for match in re.finditer(r'\b'+re.escape(name)+r'\s*\(',masked):
        begin=match.end()-1;end=closing(masked,begin)
        prefix=masked[masked.rfind('\n',0,match.start())+1:match.start()]
        # Definitions and declarations keep canonical types. Calls cast their
        # actual register values; nested calls and casts are balanced explicitly.
        if re.search(r'(?:\bvoid|\bint|\bchar\s*\*)\s*$',prefix):continue
        actual=arguments(text[begin+1:end])
        if len(actual)!=len(types):raise ValueError('Canonical call arity differs: '+name)
        if any(row['return_mismatch'] for row in changes):
            before=prefix.strip();after=text[end+1:].lstrip()
            if before or not after.startswith(';'):
                raise ValueError('Changed return prototype has an observed result: '+name)
        converted=['('+kind+')('+value+')' for kind,value in zip(types,actual)]
        edits.append((begin+1,end,', '.join(converted)))
    for begin,end,value in reversed(edits):text=text[:begin]+value+text[end:]
    ledger.append({'kind':'canonical_C_prototype','symbol':name,'canonical':signature,
                   'declarations':changes,'calls_cast':len(edits),
                   'contract':'Original callee arity/types; only pointer/int ARM32 register bits cast; no result-use change.'})
    return text


def discarded_result(text,marker,evidence,ledger):
    if text.count(marker)!=1:raise ValueError('Discarded native return marker changed')
    begin=text.index('{',text.index(marker));depth=1;end=begin+1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    text=text[:end-1]+'    return 0; // result proven discarded at original in-family callers\n'+text[end-1:]
    ledger.append({'kind':'defined_discarded_result','marker':marker,'evidence':evidence,
                   'contract':'Preserve side effects; original caller cannot observe r0 result.'})
    return text


def adapt(original_path,text,root):
    original=original_path.read_text();ledger=[];before=sha(text)
    if original_path.name=='func_02057d00.c':
        old = '    u32 ap = (((u32)&fmt) & ~3u) + 4;\n    func_02057ce8(arg0, fmt, ap);'
        if text.count(old)!=1:raise ValueError('Original stack variadic forwarding changed')
        text='#include <cstdarg>\n'+text.replace(old, '    va_list native_ap;\n    va_start(native_ap, fmt);\n    static_assert(sizeof(native_ap)==sizeof(u32), "ARM32 va_list pointer ABI");\n    u32 argument_words;\n    __builtin_memcpy(&argument_words, &native_ap, sizeof argument_words);\n    func_02057ce8(arg0, fmt, argument_words);\n    va_end(native_ap);')
        ledger.append({'kind':'ARM32_variadic_word_forward', 'symbol':'func_02057d00',
            'contract':'Forward the native saved argument words to the original parser; ARM32 only, not 64-bit varargs.',
            'proof':'overlay6-global-private-v42/smartball-format-call-proof.json'})
    if original_path.name=='func_020169f4.c':
        changes={
            'void func_02045e70(char* p);':'void func_02045e70(char* p, int native_kind);',
            'void func_020169f4(char* self)':'void func_020169f4(char* self, int native_kind)',
            '    func_02045e70(self + 8);':'    func_02045e70(self + 8, native_kind);',
        }
        for old,new in changes.items():
            if text.count(old)!=1:raise ValueError('Original ARM tail-forward shape changed: '+old)
            text=text.replace(old,new)
        ledger.append({'kind':'original_register_tail_forward',
            'symbol':'func_020169f4','register':'r1','argument':'native_kind',
            'proof':'overlay6-global-private-v42/trampoline-r1-forward-proof.json',
            'original_flow':'020169f4 loads target02045e70, adds8 to r0, BX target; r1 unchanged. Callee copies r1 to r6 and forwards it to02045ec4, which ORs r1<<16 into per-material flags.',
            'original_caller':'func_ov006_021228bc passes selected u16 data_ov006_0212f144[idx] in r1',
            'contract':'Restore omitted second formal register argument; preserve its32bits and original tail-forward value.'})
    if original_path.name=='dScMgRoulette_c.cpp':
        marker='    c->obj.v3();'
        if text.count(marker)!=1:raise ValueError('Roulette embedded virtual receiver changed')
        # GCC devirtualizes a by-value phantom ObjV member to an undefined
        # ObjV::v3. Retail instead loads the live vptr at receiver+0x10 and
        # calls slot+0xc. Preserve this explicit ARM32 dynamic dispatch.
        replacement='''    {
        void* native_receiver = raw + 0x10;
        unsigned native_vptr;
        __builtin_memcpy(&native_vptr, native_receiver, sizeof native_vptr);
        void (*native_slot)(void*);
        __builtin_memcpy(&native_slot, (const void*)(native_vptr + 0x0c), sizeof native_slot);
        native_slot(native_receiver);
    }'''
        text=text.replace(marker,replacement)
        ledger.append({'kind':'retail_dynamic_embedded_virtual_dispatch',
            'symbol':'func_ov006_02107db8','receiver_offset':16,'slot_offset':12,
            'original_instructions':['02107dcc ADD r0,r4,#16','02107de8 LDR r1,[r0]',
                                     '02107dec LDR r1,[r1,#12]','02107df0 BLX r1'],
            'proof':'overlay6-global-private-v42/roulette-virtual-dispatch-proof.json',
            'contract':'Read the live address-point and call slot3 with the original subobject receiver; no phantom method definition or stub.'})
    if original_path.suffix=='.c' and re.search(r'\bthis\b',masked_comments(original)):
        # `this` is a legal C identifier; it is reserved in generated C++.
        text,count=re.subn(r'\bthis\b','native_C_receiver',text)
        ledger.append({'kind':'C_reserved_identifier','before':'this','after':'native_C_receiver',
                       'occurrences':count,'contract':'Lexical rename only; original function signature and expressions unchanged.'})
    canonical=canonical_functions(original)
    if original_path.name=='dScMgCoin_c.cpp':
        base=root/'src/minigames/d_s_mg_base.cpp'
        base_signatures=canonical_functions(base.read_text())
        for name in ('func_ov004_020b0380','RenderOamMainScreen'):
            signature=base_signatures[name]
            signature['original_owner']=str(base)
            signature['original_owner_sha256']=sha(base.read_text())
            canonical[name]=signature
    if original_path.name=='func_ov006_020e64e4.c':
        for name in ('func_ov006_020e668c','func_ov006_020e5ffc'):
            owner=root/'src'/(name+'.c')
            signature=canonical_functions(owner.read_text())[name]
            signature['original_owner']=str(owner)
            signature['original_owner_sha256']=sha(owner.read_text())
            canonical[name]=signature
    # Only the six compile-failed owners need this new prototype adaptation.
    # Canonical helper definitions are discovered, not an actor-name list.
    if original_path.name in ('dScMgCoin_c.cpp','dScMgPanel_c.cpp','func_ov006_020e64e4.c'):
        for name,signature in canonical.items():
            text=canonicalize(text,name,signature,ledger)
    # Anonymous typedefs have no struct tag in GCC. Correct explicit tag use
    # only when this exact original TU defines the anonymous typedef.
    # This shape is enabled only for the actual GCC-failed owner. Applying it
    # blindly across namespaces could conflate an unrelated same-named tag.
    aliases=re.findall(r'typedef\s+struct\s*\{[^}]*\}\s*(\w+)\s*;',original,re.S) if original_path.name=='dScMgSmartball_c.cpp' else []
    for name in aliases:
        text,count=re.subn(r'\bstruct\s+'+re.escape(name)+r'\b',name,text)
        if count:ledger.append({'kind':'anonymous_typedef_tag','type':name,'uses':count,
                               'contract':'Use the exact existing anonymous-struct typedef; no layout change.'})
    if original_path.name=='dScMgSlots.cpp':
        text=discarded_result(text,'int dScMgSlot1_c::OnHitFromUnderneath()\n',
            {'header':'include/dScMgBase_c.h, slot28 provenance',
             'original_discarded_dispatch_sites':['0x020aedec','0x020aee90','0x020b06cc'],
             'source_caller':'src/minigames/d_s_mg_base.cpp:BeforeBehavior',
             'source_direct_reference_inventory':{'slot_method':'ov006 relocation0213ebb0 only',
                 'func_ov004_020aeb24':'ov004 BL020b070c only; source return void, BeforeBehavior ignores result then returns0'},
             'gate_status':'PENDING_ORIGINAL_INDIRECT_DISPATCH_INVENTORY_AND_R0_FLOW',
             'limits':'Header documents three in-family sites; this ledger alone does not prove absence of another indirect dispatch or computed-index call. No runtime/admission validation until binary inventory and callers reviewed.',
             'note':'Expected known callers discard r0, but source adaptation is still a candidate pending independent ROM gate.'},ledger)
    if original_path.name=='dScMgSmartball_c.cpp':
        marker='int dScMgSmartball_c::Virtual7C()\n'
        if text.count(marker)!=1:raise ValueError('Smartball Virtual7C shape changed')
        start=text.index(marker);end=text.index('\n}',start)
        body=text[start:end]
        before_call='    LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());'
        if body.count(before_call)!=1:raise ValueError('Smartball last loader call changed')
        body=body.replace(before_call,'    return LoadCompressedFileAt(0x5b, _ZN3G2S12GetBG1ScrPtrEv());')
        text=text[:start]+body+text[end:]
        ledger.append({'kind':'preserve_last_callee_return','symbol':'_ZN16dScMgSmartball_c9Virtual7CEv',
            'original_address':'0x02118ae4','evidence':'Original own method ends in LoadCompressedFileAt; own source caller BeforeInitResources invokes Virtual7C then returns1. Relocation inventory finds0213ef78 as sole method-address reference.',
            'gate_status':'PENDING_ORIGINAL_SMARTBALL_METHOD_R0_FLOW_AND_DISPATCH_INVENTORY',
            'limits':'No validation by analogy with Amida. Original own instruction tail and every relevant caller must be checked before runtime/admission proof.',
            'contract':'Return the existing last loader result, preserving call order and side effects.'})
    if ledger:
        text='static_assert(sizeof(void*)==4 && sizeof(int)==4,"native batch requires ARM32 register ABI");\n'+text
    return text,{'original':str(original_path),'original_sha256':sha(original),
                 'generated_before_sha256':before,'generated_after_sha256':sha(text),'adaptations':ledger}
