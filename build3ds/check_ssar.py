#!/usr/bin/env python3
"""Compare the native SSAR reader against ndspy and exercise every ROM entry."""
from pathlib import Path
import json,subprocess,hashlib
import ndspy.soundArchive
root=Path(__file__).resolve().parent.parent
sdat=root/'sm64ds-decomp/extracted/dsd/files/data/sound_data.sdat'
archive=ndspy.soundArchive.SDAT.fromFile(sdat)
out=root/'build3ds/validation/ssar-native-host';out.mkdir(parents=True,exist_ok=True)
def fnv(data):
 h=2166136261
 for b in data:h=((h^b)*16777619)&0xffffffff
 return h
rows=[];errors=[]
# Controlled cache prerequisites: same SBNK file, full voice/system bindings.
primers={(1,34):6,(1,92):7,(1,98):7,(1,162):8,(1,226):9,(2,57):21,(2,102):21}
for ai,(name,a) in enumerate(archive.sequenceArchives):
 events=fnv(a.eventsData)
 for si,(_,seq) in enumerate(a.sequences):
  row={'archive':ai,'sequence':si,'name':name}
  if (ai,si) in primers:row['prior_bank']=primers[ai,si]
  try:
   r=subprocess.run([str(root/'build3ds/probe_ssar'),str(sdat),str(ai),str(si)]+(['32'] if ai==3 else ['-1',str(primers[ai,si])] if (ai,si) in primers else []),capture_output=True,text=True,timeout=5)
   if seq.firstEventOffset in [-1,None]:
    assert r.returncode==5 and 'Absent SSAR sequence' in r.stderr,r.stderr
    row['absent']=True
   else:
    assert r.returncode==0,(r.returncode,r.stderr)
    got=json.loads(r.stdout)
    assert (got['offset'],got['bank'],got['volume'],got['events_hash'])==(seq.firstEventOffset,32 if ai==3 else seq.bankID,seq.volume,events),got
    row.update(got)
  except Exception as e:row['error']=repr(e);errors.append(row)
  rows.append(row)
 print(name,'checked',len(a.sequences),flush=True)
report={'sdat_sha256':hashlib.sha256(sdat.read_bytes()).hexdigest(),'frames_per_entry':32768,'sample_rate':32768,'rows':rows,'errors':errors}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('present',sum('pcm_hash' in r for r in rows),'absent',sum(r.get('absent',False) for r in rows),'silent',sum(r.get('nonzero')==0 for r in rows),'errors',len(errors))
if errors:print(errors[:10]);raise SystemExit(1)
