#!/usr/bin/env python3
"""Compare opening-letter clip vertices with an untouched DS GX capture."""
import argparse,json,re
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parent
source=root/'reference-ds/cutscene/step-0435.polygons'
polygons=re.split(r'(?=^poly )',source.read_text(),flags=re.M)
paper=[p for p in polygons if re.match(r'poly \d+ attr 19168081 .* num 4 ',p)][-1]
expected={}
for match in re.finditer(r'clip (-?\d+) (-?\d+) (-?\d+) (-?\d+) uv (-?\d+) (-?\d+)',paper):
    values=list(map(int,match.groups()));expected[tuple(v/16 for v in values[4:])]=values[:4]
actual={}
for line in (args.run/'log.txt').read_text().splitlines():
    match=re.match(r'Stage letter \d+/\d+: .* clip=([^ ]+) uv=([^ ]+)',line)
    if match:
        uv=tuple(map(float,match[2].split(',')))
        if uv in expected:actual[uv]=[round(float(v)*4096) for v in match[1].split(',')]
comparisons=[dict(uv=uv,expected=value,actual=actual.get(uv),exact=actual.get(uv)==value) for uv,value in expected.items()]
result=dict(passed=len(comparisons)==4 and all(c['exact'] for c in comparisons),source=str(source),comparisons=comparisons)
(args.run/'letter-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
raise SystemExit(0 if result['passed'] else 1)
