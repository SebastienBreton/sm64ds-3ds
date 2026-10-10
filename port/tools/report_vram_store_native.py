#!/usr/bin/env python3
"""Assess the completed original-body/physical-bus standalone ARM11 run."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive',type=Path)
OUT=parser.parse_args().archive.resolve()
path=OUT/'report.json'
report=json.loads(path.read_text())
for name,digest in report['archive_sha256'].items():
    assert hashlib.sha256((OUT/name).read_bytes()).hexdigest()==digest,name
run=json.loads((OUT/'native-old/run.json').read_text())
assert run['result']==run['application_result']=='PASS'
assert run['unmapped_access_count']==0 and run['emulator_log_fresh']
assert run['requested_3ds_model']=='old' and not run['model_verification_failure']
assert run['temporary_settings_restored']
assert run['original_config_sha256']==run['restored_config_sha256']
assert run['sha256']==report['archive_sha256']['smoke_physical_vram_stores.3dsx']
log=(OUT/'native-old/log.txt').read_text()
assert 'CPU mode: model_query=00000000 new3ds=0 speedup_requested=0' in log
native=next(line for line in log.splitlines() if line.startswith('PASS fixtures='))
host=next(line for line in report['host'].splitlines() if line.startswith('PASS fixtures='))
assert native==host,(native,host)
assert int(re.search(r'fixtures=(\d+)',native)[1])==report['golden']['cases']
old=ROOT/'port/tests/vram_transfer_fixtures.h'
old_proof=json.loads((ROOT/'build3ds/validation/physical-vram-transfers/golden-proof.json').read_text())
old_sha=hashlib.sha256(old.read_bytes()).hexdigest()
assert old_sha==old_proof['header_sha256']
report['existing_nine_transfer_fixtures_unchanged_sha256']=old_sha
report['status']='PASS original ARM goldens + host ASan/UBSan (explicit pointer adaptation) + exact ARM11 classic ABI; production not integrated'
report['native']={'status':'PASS','runner':run,'fixture_result':native,
                  'host_native_results_identical':True,'shared_objects_used':False,
                  'dispatch':'GNU --wrap with unrenamed original bodies and original 32-bit types/DS pointers.'}
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
report['archive_sha256']={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='report.json'}
path.write_text(json.dumps(report,indent=2)+'\n')
print(report['status']);print(native);print('old fixtures unchanged; config restored; zero Unmapped')
