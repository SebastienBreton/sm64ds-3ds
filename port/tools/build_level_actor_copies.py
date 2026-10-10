#!/usr/bin/env python3
"""Compile inventoried missing actor overlays privately; report every failure.

This is source/ARM11 compilation preparation only. No data/lifecycle closure,
link success, ABI correctness or native runtime support is implied.
"""
import argparse, hashlib, importlib.util, json, re, shlex, shutil, subprocess
from pathlib import Path
from level_actor_cpp_adapters import adapt
from native_vtable_stores import adapt as adapt_vptr

ROOT=Path(__file__).resolve().parents[2]
PORT=ROOT/'port'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--native-cpp-adaptations',action='store_true')
    parser.add_argument('--native-vtable-proof',type=Path,
        help='Linked ELF proof of GCC prefixes for explicit ARM9 primary-vptr stores')
    parser.add_argument('--dependency-report',type=Path,
        help='Also compile unresolved original function TUs from an object-edge audit')
    args=parser.parse_args()
    proven_tables = set()
    if args.native_vtable_proof:
        proof=json.loads(args.native_vtable_proof.read_text())
        assert proof['status']=='PASS_NATIVE_GCC_PRIMARY_VTABLE_PREFIX'
        for table in proof['tables']:
            assert table['native_address_point']==table['native_vtable_address']+8
            assert table['words'][0]==0 and table['words'][1]
            proven_tables.add(table['symbol'])
    original=args.inventory.resolve();out=args.output.resolve()
    assert not out.exists()
    audit=json.loads(original.read_text());root=PORT.parent
    names=sorted({p for o in audit['overlay_inventory']['overlays'] if o['overlay'] in audit['unsupported_overlays']
        for p in o['source_translation_units']})
    assert all(sha(root/n)==audit['copied_original_tu_sha256'][n] for n in names)
    if args.dependency_report:
        dependency=json.loads(args.dependency_report.read_text())
        names=sorted(set(names)|{r['original_source'] for r in dependency['edges']
            if r['resolution']=='unresolved' and r['original_source'] and not r['symbol'].startswith('data_')})
    source_hashes={n:sha(root/n) for n in names}
    out.mkdir(parents=True)
    hostgen=PORT/'tools/hostgen.py'
    initial=sha(hostgen)
    spec=importlib.util.spec_from_file_location('private_hostgen',hostgen)
    hg=importlib.util.module_from_spec(spec);spec.loader.exec_module(hg)
    flags_path=ROOT/'build3ds/cmake/CMakeFiles/smoke_player.dir/flags.make'
    text=flags_path.read_text()
    def flags_for(language):
        return [v for k in (language+'_DEFINES',language+'_INCLUDES',language+'_FLAGS')
            for v in shlex.split(re.search(r'^'+k+r' = (.*)$',text,re.M)[1])]
    shutil.copy2(flags_path,out/'flags.make');shutil.copy2(hostgen,out/'hostgen-frozen.py')
    rows=[]
    for n in names:
        source=root/n;generated,rewrites=hg.emit(source,out/'generated',root,True)
        # Real C includes old unspecified argument declarations. Converting
        # these to C++ changes their meaning. Keep the language when no memory
        # rewrite is needed; a //cpp marker explicitly opts into C++ instead.
        native_c=(source.suffix=='.c' and rewrites==0
            and not source.read_text().lstrip().startswith('//cpp'))
        if native_c:
            generated=out/'generated'/n
            generated.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,generated)
        adaptations=[]
        if args.native_cpp_adaptations and not native_c:
            changed,adaptations=adapt(n,generated.read_text())
            generated.write_text(changed)
        vptr_adaptations=[]
        if args.native_vtable_proof:
            changed,vptr_adaptations=adapt_vptr(generated.read_text(),proven_tables)
            generated.write_text(changed)
        language='C' if native_c else 'CXX'
        flags=flags_for(language)
        obj=out/'objects'/Path(n).with_suffix('.o');obj.parent.mkdir(parents=True,exist_ok=True)
        log=out/'logs'/Path(n).with_suffix('.log');log.parent.mkdir(parents=True,exist_ok=True)
        compiler='arm-none-eabi-gcc' if native_c else 'arm-none-eabi-g++'
        cmd=['/opt/devkitpro/devkitARM/bin/'+compiler,*flags,'-I'+str(PORT/'platform/3ds'),
            '-MMD','-MF',str(obj.with_suffix('.d')),'-c',str(generated),'-o',str(obj)]
        p=subprocess.run(cmd,text=True,capture_output=True)
        log.write_text(p.stdout+p.stderr)
        rows.append({'source':n,'source_sha256':sha(source),'generated':str(generated.relative_to(out)),
            'generated_sha256':sha(generated),'rewrites':rewrites,'adaptations':adaptations,
            'native_vptr_adaptations':vptr_adaptations,
            'language':language,'command':cmd,'exit_code':p.returncode,
            'object_sha256':sha(obj) if obj.is_file() else None,'log':str(log.relative_to(out))})
        print(('PASS' if p.returncode==0 else 'FAIL')+' '+n,flush=True)
    assert sha(hostgen)==initial
    assert all(sha(root/n)==source_hashes[n] for n in names)
    result={'status':'PASS original actor copies ARM11 compilation only' if all(r['exit_code']==0 for r in rows)
        else 'FAIL some original actor copies require native compile adaptation; no closure PASS',
        'production_modified':False,'level':audit['level'],'unsupported_overlays':audit['unsupported_overlays'],
        'translation_units':len(rows),'compiled':sum(r['exit_code']==0 for r in rows),'rows':rows,
        'inventory_sha256':sha(original),'hostgen_sha256':initial,
        'dependency_report_sha256':sha(args.dependency_report) if args.dependency_report else None,
        'native_vtable_proof_sha256':sha(args.native_vtable_proof) if args.native_vtable_proof else None,
        'limits':['Data/PMF/vtable relocation, native lifecycle and linker dependency closure pending',
            'No original source edits, shared objects or CMake mutation','No native runtime or Bob gameplay PASS']}
    shutil.copy2(Path(__file__),out/Path(__file__).name)
    shutil.copy2(Path(__file__).with_name('level_actor_cpp_adapters.py'),out/'level_actor_cpp_adapters.py')
    shutil.copy2(Path(__file__).with_name('native_vtable_stores.py'),out/'native_vtable_stores.py')
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(result['status'],result['compiled'],'/',len(rows))


if __name__=='__main__':main()
