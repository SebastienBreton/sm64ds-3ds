#!/usr/bin/env python3
"""Verify archived integration against the frozen, natively tested candidate.

Read-only with respect to production; writes an integration proof and README.
It never builds the common graph or launches an emulator.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive', type=Path, default=ROOT / 'build3ds/validation/fixed-direction-integrated')
args = parser.parse_args()
out = args.archive.resolve()
prior = ROOT / 'build3ds/validation/fixed-direction-bounded-matrix-bit-neighbours'
report = json.loads((out / 'report.json').read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for relative, expected in report['archive_sha256'].items():
    assert sha(out / relative) == expected, relative
assert report['integrated_production_under_test']
assert report['vectors'].startswith('PASS vectors=79124 ')
production = ROOT / 'port/ntr/include/ntr/fixed_direction_matrix.h'
gx = ROOT / 'port/ntr/gx.cpp'
assert sha(production) == sha(out / 'include/ntr/fixed_direction_matrix.h')
assert sha(gx) == sha(out / 'gx-current.cpp')
candidate = out / 'include/fixed_direction_bounded_candidate.h'
assert sha(candidate) == sha(prior / 'include/fixed_direction_bounded_candidate.h')

# Compare the entire class token stream, removing exactly the test-only stats
# declaration and its four guarded increments. Arithmetic/cache tokens survive.
c = candidate.read_text()
c = c[c.index('class FixedDirectionBoundedCandidate'):c.rindex('};') + 2]
c = c.replace('FixedDirectionBoundedCandidate', 'BoundedFixedDirectionMatrix')
stats = '    struct Statistics { std::uint64_t hits=0,misses=0,bounded_misses=0,fallback_misses=0; } stats;\n'
assert c.count(stats) == 1
c = c.replace(stats, '')
for line in ['            if constexpr(Count)++stats.hits;\n',
             '        if constexpr(Count)++stats.misses;\n',
             '            if constexpr(Count)++stats.fallback_misses;\n']:
    assert c.count(line) == 1
    c = c.replace(line, '')
line = 'else if constexpr(Count)++stats.bounded_misses;'
assert c.count(line) == 1
c = c.replace(line, '')
p = production.read_text()
p = p[p.index('class BoundedFixedDirectionMatrix'):p.rindex('};') + 2]


def tokens(text):
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    return re.findall(r'\w+|[^\s\w]', text)


assert tokens(c) == tokens(p), 'Counter-free candidate and production tokens differ'
assert 'stats' not in tokens(p) and 'Count' not in tokens(p)
normalized = '\n'.join(tokens(p)) + '\n'
(out / 'counter-free-class.tokens').write_text(normalized)
asm = (out / 'gx-integrated-native-disassembly.log').read_text()
start = asm.index('Disassembly of section .text._ZN3ntr27BoundedFixedDirectionMatrix9transform')
end = asm.index('Disassembly of section', start + 1)
body = asm[start:end]
counts = {name: len(re.findall(r'\t' + re.escape(name) + r'\s', body))
          for name in ['vcvt.s32.f32', 'smull', 'smlal', 'mul', 'mla', 'umull']}
assert counts['vcvt.s32.f32'] == 1
assert counts['smull'] == 1 and counts['smlal'] == 2
assert counts['mul'] == counts['mla'] == counts['umull'] == 1
assert 'R_ARM_CALL\tmemcmp' in body and 'mov\tr2, #64' in body
assert 'R_ARM_CALL\t__aeabi_d2lz' in body
assert 'R_ARM_CALL\t__aeabi_l2d' in body
(out / 'integrated-transform-arm11.asm').write_text(body)
shutil.copy2(Path(__file__), out / Path(__file__).name)
proof = {
    'status': 'PASS exact candidate-to-production class tokens after explicit counter removal; production GX ARM11 instruction contract',
    'production_sha256': {str(production): sha(production), str(gx): sha(gx)},
    'prior_candidate_sha256': sha(candidate),
    'prior_native_report_sha256': sha(prior / 'report.json'),
    'counter_free_tokens_sha256': sha(out / 'counter-free-class.tokens'),
    'removed_test_only_elements': ['Statistics declaration', 'hits increment', 'misses increment', 'fallback_misses increment', 'bounded_misses else/increment'],
    'arm11_transform_instruction_counts': counts,
    'arm11_multiplication_scope': 'One SMULL and two SMLAL instructions are in the loop executing each of three axes. The fallback retains MUL/MLA/UMULL.',
    'bounded_domain': 'All nine consumed coefficients: float * 4096 in [-2147483648,2147483648). Any ineligible coefficient recomputes all nine with the unchanged legacy double/int64 rounding.',
    'sum_absolute_upper_bound': 3298534883328,
    'shifted_absolute_upper_bound': 805306368,
    'cache_scope': 'Same full 64-byte memcmp and source copy; no generation scheme or runtime counters.',
    'native_integrated_execution': 'NOT_EXECUTED; prior counter-free candidate native run is separate evidence, not a new integrated runtime pass.',
}
(out / 'integration-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
(out / 'README.md').write_text('''# Bounded direction matrix production integration

`ntr::BoundedFixedDirectionMatrix` is now the production NORMAL/LIGHT_VECTOR
cache in `lighting_vector`. It preserves all 64 source bits in the cache key,
half-away rounding, packed signed10 input, signed11 wrapping and the existing
double/int64 fallback. No runtime counters or generation cache were added.
The existing cached64 `FixedDirectionMatrix` remains the reference/API.

The actual integrated class passes 79,124 vectors against cached64 under
ASan/UBSan, including signed extrema, float bit neighbours, fallback transitions,
unused-word bit changes and deterministic random matrices. Output checksum is
4917871271584117422; matrix-input checksum is 16988124851289549652.
The independently counted frozen candidate records 76,501 hits, 2,623 misses,
2,229 bounded misses and 394 fallback misses. Production has no counters:
`integration-proof.json` verifies its whole class token stream is the candidate
with only the test stats declaration/increments and class name removed.

Actual production GX passes the independent DS diffuse (135 cases), specular
(1,728 cases, 948 nonblack), and texgen (576 cases) oracles under ASan/UBSan.
The frozen candidate GX also passes identical oracle outputs. Oracle edits are
limited to freeing their six allocations before exit for LeakSanitizer.

The actual integrated GX ARM11 object compiles without warnings. Its transform
has the same memcmp64 cache and legacy double/int64 conversions. The bounded
path uses VCVT.S32.F32 and one SMULL plus two SMLAL instructions in the loop for
each axis; fallback MUL/MLA/UMULL is retained. Object/source/dependency hashes,
commands and the extracted assembly are archived. No common graph was mutated.

The bounded matrix domain requires all nine scaled coefficients in the int32
interval [-2147483648,2147483648). Its three-product sum magnitude is at most
3,298,534,883,328, and after shift at most 805,306,368, safe for int32/negation.
Any ineligible value sends all nine through legacy conversion. Consumed NaN,
infinity and legacy arithmetic overflow remain outside defined execution;
the integration does not invent behavior for them.

This integration archive makes no new timing or emulator execution claim.
Its standalone ARM11 binary is linked but NOT_EXECUTED. The earlier archive
`fixed-direction-bounded-matrix-bit-neighbours` separately passed in classic
Azahar with identical input/output checksums and cache counters, zero Unmapped,
and restored configuration. Its candidate header SHA is verified unchanged.
The parent's separate real-menu A/B reported Render 31.377249 to 29.453905 ms,
six identical BMPs and zero Unmapped; this is prior evidence, not a new run of
the integrated production binary. No whole-game or 60 FPS claim is made.

Reproduce the autonomous integration checks with:
`python3 port/tools/audit_fixed_direction_bounded.py --integrated --output build3ds/validation/fixed-direction-integrated`
and the class/assembly proof with:
`python3 port/tools/report_fixed_direction_integrated.py`.
Neither tool launches Azahar or changes CMake.
''')
report['integration_proof'] = proof
report['archive_sha256'] = {str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*'))
                            if p.is_file() and p.name != 'report.json'}
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(proof['status'])
print(json.dumps(proof['production_sha256'], indent=2))
