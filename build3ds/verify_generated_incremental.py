#!/usr/bin/env python3
"""Check actual generated-source bytes/timestamps and stale closure cleanup."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build3ds/validation/generated-tree-incremental'
OUT.mkdir(parents=True,exist_ok=True)
helper=ROOT/'port/tools/sync_generated_tree.py'
spec=importlib.util.spec_from_file_location('generated_sync',helper)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='sm64ds-generated-check-') as directory:
    temporary=Path(directory)
    source,destination=temporary/'source',temporary/'destination'
    source.mkdir();destination.mkdir()
    (source/'a.cpp').write_text('old')
    (destination/'a.cpp').write_text('old')
    (destination/'obsolete.cpp').write_text('removed')
    (destination/'custom.c').write_text('preserved')
    stamp=(destination/'a.cpp').stat().st_mtime_ns
    assert module.sync(source,destination,['custom.c'])==(0,1,1)
    assert stamp==(destination/'a.cpp').stat().st_mtime_ns
    assert (destination/'custom.c').read_text()=='preserved'
    (source/'a.cpp').write_text('new')
    (source/'b.cpp').write_text('added')
    assert module.sync(source,destination,['custom.c'])==(2,0,0)
    assert (destination/'a.cpp').read_text()=='new'
    assert module.sync(source,destination,['custom.c'])==(0,0,2)


def snapshot():
    trees=[ROOT/'build3ds/cmake'/name for name in ['menu-native','wanted-native']]
    return {str(path):{'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'mtime_ns':path.stat().st_mtime_ns}
            for tree in trees for path in tree.rglob('*')
            if path.is_file() and path.suffix in ['.c','.cpp','.h']}


before=snapshot()
with (OUT/'build.log').open('w') as log:
    result=subprocess.run(['cmake','--build',str(ROOT/'build3ds/cmake'),
                           '--target','smoke_player_3dsx','-j8'],stdout=log,stderr=subprocess.STDOUT)
after=snapshot()
report={'status':'PASS' if result.returncode==0 and before==after else 'FAIL',
        'build_returncode':result.returncode,
        'generated_source_count':len(before),
        'source_bytes_and_timestamps_unchanged':before==after,
        'stale_removal_and_custom_output_preservation':'PASS',
        'before':before,'after':after}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(report['status'],len(before),'generated sources; bytes and timestamps unchanged:',before==after)
assert result.returncode==0 and before==after
