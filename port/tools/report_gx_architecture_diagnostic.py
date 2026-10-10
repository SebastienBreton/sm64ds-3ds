#!/usr/bin/env python3
"""Summarize bounded architectural diagnostics without calling them net budgets."""
from pathlib import Path
import hashlib
import json
import re

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build3ds/validation/gx-architecture-diagnostic'
BASE=ROOT/'build3ds/validation/architecture-integrated-baseline/menu-repeat/current'
names={0:'mode0-counters',1:'mode1-ingress',2:'mode2-command-groups',4:'mode4-bulk'}
runs={}
for mode,name in names.items():
    directory=OUT/name
    run=json.loads((directory/'run.json').read_text())
    validation=json.loads((directory/'validation.json').read_text())
    assert run['result']=='PASS' and run['unmapped_access_count']==0
    assert run['emulator_log_fresh'] and run['temporary_settings_restored']
    assert validation['save']['restored']
    assert len(validation['images_sha256'])==6
    assert all(v['baseline']==v['diagnostic'] for v in validation['images_sha256'].values())
    text=(directory/'log.txt').read_text()
    phases={int(i):{'calls':int(c),'samples':int(s),'ticks':int(t),'raw_estimate_ms':float(e)}
        for i,c,s,t,e in re.findall(r'GX diagnostic phase=(\d+).*? calls=(\d+) samples=(\d+) ticks=(\d+).*? estimated_ms/update=([\d.]+)',text)}
    commands={cmd:int(count) for cmd,count in re.findall(r'GX diagnostic command=([0-9a-f]+) calls=(\d+)',text)}
    clock_min=int(re.search(r'clock_pair_min_ticks=(\d+)',text)[1])/268111.856
    for phase in phases.values():
        phase['clock_subtraction_illustration_ms']=phase['raw_estimate_ms']-phase['calls']/120*clock_min if phase['samples'] else None
    runs[mode]={'render_ms':float(re.search(r'Render/GX decode=([\d.]+)',text)[1]),
        'clock_pair_min_ms':clock_min,'commands':commands,'phases':phases,'run':run,
        'save':validation['save']}
assert all(r['commands']==runs[0]['commands'] for r in runs.values())
assert len({r['run']['sha256'] for r in runs.values()})==1
base_text=(BASE/'log.txt').read_text()
base_render=float(re.search(r'Render/GX decode=([\d.]+)',base_text)[1])
ingress_raw=sum(runs[1]['phases'][i]['raw_estimate_ms'] for i in [0,1])
exec_raw=sum(runs[2]['phases'][i]['raw_estimate_ms'] for i in range(2,7))
ingress_adjusted=sum(runs[1]['phases'][i]['clock_subtraction_illustration_ms'] for i in [0,1])
exec_adjusted=sum(runs[2]['phases'][i]['clock_subtraction_illustration_ms'] for i in range(2,7))
report={'status':'PASS four classic diagnostic runs, zero Unmapped, identical six baseline BMPs per run and workloads, save/config restored',
    'optimization_gain_claim':False,'baseline_render_ms':base_render,
    'counters_only_render_ms':runs[0]['render_ms'],'counters_perturbation_ms':runs[0]['render_ms']-base_render,
    'raw_ingress_estimate_ms':ingress_raw,'raw_exec_estimate_ms':exec_raw,
    'illustrative_clock_adjusted_ingress_ms':ingress_adjusted,
    'illustrative_clock_adjusted_exec_ms':exec_adjusted,
    'illustrative_difference_ms':ingress_adjusted-exec_adjusted,
    'non_bulk_residual_in_diagnostic_ms':runs[4]['render_ms']-runs[4]['phases'][13]['raw_estimate_ms'],
    'limitations':['Fixed stride/one offset may alias workloads; no repeated-offset confidence interval.',
        'Minimum empty clock pair is not exact per-scope overhead; illustrative subtraction is not a net budget.',
        'Counters/scopes perturb Render by approximately6.45ms and some overhead lies outside sampled ingress.',
        'Bulk excludes Copy48BytesFixed and physical register-group branches; residual includes non-bulk GX and emission.',
        'Equal command counts/BMPs complement host goldens, but do not alone prove full bytewise GX state.',
        'No parser cache or direct-arena candidate executed; no throughput/stereo/whole-game gain claimed.'],
    'decision':'Do not spend the architectural budget on parser alone. Keep decoding IR as execution infrastructure; prioritize indexed representation, direct arena preparation and dependency-safe reuse.',
    'updated_global_gate_parent_evidence':{'new_ppu_menu_update_ms':58.760,'target_ms':48.133,'additional_saving_required_ms':10.627,
        'scope':'Parent separately reports real PPU architecture A/B with exact BMPs; this diagnostic does not reproduce it.'},
    'runs':runs}
(OUT/'runtime-report.json').write_text(json.dumps(report,indent=2)+'\n')
(OUT/'RESULTS.md').write_text(f'''# GX architectural budget diagnostic

All four runs pass classic/old, zero Unmapped, identical command counts and six
byte-identical baseline BMPs each. Config and private saves are restored.
One frozen binary serves modes 0/1/2/4. No production file or shared graph changed.

Baseline Render is {base_render:.6f} ms. Counters-only Render is
{runs[0]['render_ms']:.6f} ms: instrumentation adds
{runs[0]['render_ms']-base_render:.6f} ms before sample clocks. This is a diagnostic,
not a speed comparison. The old fine profile's 55 ms Render is not reused.

| Scope | Raw sample estimate ms/update | Illustration after minimum clock subtraction |
|---|---:|---:|
| GX FIFO + ports, inclusive | {ingress_raw:.3f} | {ingress_adjusted:.3f} |
| All exec groups, inclusive | {exec_raw:.3f} | {exec_adjusted:.3f} |
| Runtime copy_words blocks, inclusive | {runs[4]['phases'][13]['raw_estimate_ms']:.3f} | {runs[4]['phases'][13]['clock_subtraction_illustration_ms']:.3f} |

The raw per-word estimate exceeds actual Render because the final clock read is
extrapolated across 20,265 writes/update. Minimum clock-pair time is 1.312885 µs.
Subtracting it gives an *illustration*, not calibrated net function time. The
approximately {ingress_adjusted-exec_adjusted:.3f} ms ingress-minus-exec difference
is a hypothesis for decoder/dispatch/parameter copying, not a proved removable
parser budget. Validation, stream hashing, partial packets and replay remain.

The command-group illustrations are matrix 1.916, NORMAL 7.738, vertices 9.459,
texture binding 3.040, other 1.114 ms. They include counter effects and sampling
error. Projection/lighting cannot be inferred independently from these groups,
and nested phase estimates must never be added.

Macro bulk measures approximately 23.728 ms, with only 0.319 ms extrapolated
minimum clock read. Actual Render in that mode is 35.920 ms; its approximately
12.192 ms residual contains original game work, non-bulk GX and instrumentation.
It is not a pure game-emission measurement. Root's new PPU A/B separately saves
13.432 ms/update (72.192 to 58.760), leaving a 10.627 ms additional global saving
to reach 48.133 ms/+50% throughput. That reduced target remains unproved for GX.

Workload per update: 16,827.7 FIFO words, 3,437.4 port writes, 3,648.8 vertices,
2,657 normals, 2,661 directions, 1,335.3 matrix commands, 150.4 texture binds,
2,310.4 triangles. copy_words sees 243 RAM-or-other FIFO blocks, 14,698 words;
47 enter with empty decoder and 196 with a pending command. No VRAM/IO sources
appear in these helpers. Other helpers and direct ports remain separate.

Decision: do not treat parser as the major performance milestone. The indexed
representation/direct arena sink can remove repeated triangle expansion,
conversion and vector-to-VBO copying. Reusing transformed/lit geometry also
requires exact incoming matrix/resource/state dependencies and side effects.
The CPU DS fixed12, clipping, rounding and alpha contracts remain mandatory.

Limits: one stride-64 offset, no confidence interval; minimum clock subtraction
is illustrative; counts-only overhead is substantial; equal counts and BMPs
are not a complete state oracle. No native cache/arena candidate or new gain has
been measured. Emulated timings do not establish physical ARM11 performance.
The next prototype is isolated and differential, coordinated with FPS's arena
ownership. Full state/geometry/resource parity precedes production integration.
''')
artifacts={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest()
           for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='runtime-manifest.json'}
(OUT/'runtime-manifest.json').write_text(json.dumps(artifacts,indent=2)+'\n')
print(report['status']);print('Parser-only budget remains unproved; direct representation is next.')
