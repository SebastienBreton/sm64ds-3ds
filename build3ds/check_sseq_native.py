#!/usr/bin/env python3
"""Build the pinned candidate synth and render five seconds of every local SSEQ.
Run with .venv/bin/python; requires the local ROM extraction and cloned candidate.
Nonzero PCM is a smoke test, not a DS fidelity comparison or a3DS benchmark.
"""
from pathlib import Path
from ndspy.soundArchive import SDAT
import hashlib,json,math,struct,subprocess,wave
root=Path(__file__).resolve().parent.parent
vendor=root/'build3ds/vendor/SSEQPlayer'
expected='77222d3657adff358fb4e610d3e56bb7ada8ec24'
commit=subprocess.check_output(['git','-C',str(vendor),'rev-parse','HEAD'],text=True).strip()
assert commit==expected,'Review updated upstream before comparing results'
exe=root/'build3ds/probe_sseq'
subprocess.run(['c++','-std=c++17','-O2','-I',str(vendor),str(root/'build3ds/probe_sseq.cpp'),*[str(p) for p in sorted(vendor.glob('*.cpp'))],'-o',str(exe)],check=True)
rom=root/'sm64ds-decomp/extracted/dsd/files/data/sound_data.sdat'
out=root/'build3ds/validation/sseq-native-host';out.mkdir(parents=True,exist_ok=True)
sdat=SDAT.fromFile(str(rom));records=[]
for ident,(name,seq) in enumerate(sdat.sequences):
    if seq is None:continue
    pcm=out/'probe.pcm'
    run=subprocess.run([str(exe),str(rom),str(ident),str(pcm)],capture_output=True,text=True,timeout=30)
    row=dict(id=ident,name=name,exit=run.returncode,log=run.stdout+run.stderr)
    if run.returncode==0:
        raw=pcm.read_bytes();samples=struct.unpack('<'+str(len(raw)//2)+'h',raw)
        row.update(bytes=len(raw),peak=max(abs(x) for x in samples),rms=math.sqrt(sum(x*x for x in samples)/len(samples)),sha256=hashlib.sha256(raw).hexdigest())
        if ident==55:
            with wave.open(str(out/'title.wav'),'wb') as w:
                w.setnchannels(2);w.setsampwidth(2);w.setframerate(32768);w.writeframes(raw)
    records.append(row)
pcm.unlink(missing_ok=True)
(out/'report.json').write_text(json.dumps(dict(sdat_sha256=hashlib.sha256(rom.read_bytes()).hexdigest(),upstream_commit=commit,sequences=records),indent=2))
failures=[r for r in records if r['exit'] or not r.get('peak')]
print(f"{len(records)-len(failures)}/{len(records)} sequences generated nonzero PCM; SSAR and physical output untested")
raise SystemExit(bool(failures))
