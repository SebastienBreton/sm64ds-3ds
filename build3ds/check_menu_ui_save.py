#!/usr/bin/env python3
"""Compare native menu cart transactions with separately captured retail DS results."""
from pathlib import Path
import argparse,json
p=argparse.ArgumentParser();p.add_argument("action",choices=["copy-no","copy-yes","erase-no","erase-yes"]);p.add_argument("native",type=Path);p.add_argument("output",type=Path);a=p.parse_args()
r=Path(__file__).resolve().parent/"reference-menu"
before=(r/"diagnostic-occupied.sav").read_bytes();actual=a.native.read_bytes()
assert len(actual)==8192
reference=(r/f"diagnostic-{a.action}-after.sav").read_bytes()
# Retail boot may update unrelated global blocks. Compare file-slot blocks and
# require that the native transaction itself preserved every unrelated byte.
changed=[i for i,(x,y) in enumerate(zip(before,actual)) if x!=y]
if a.action.endswith("no"):
    assert actual==before
else:
    dest=1 if a.action.startswith("copy") else 0
    allowed=set(range(dest*128,dest*128+78))|set(range(4096+dest*128,4096+dest*128+78))
    assert set(changed)<=allowed
    for base in [0,4096]:
        assert actual[base:base+384]==reference[base:base+384]
result={"action":a.action,"result":"PASS","changed_bytes":len(changed),"retail_reference":str(r/f"diagnostic-{a.action}-after.sav")}
a.output.write_text(json.dumps(result,indent=2)+"\n");print(result)
