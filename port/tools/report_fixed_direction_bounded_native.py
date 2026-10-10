#!/usr/bin/env python3
"""Assess a completed isolated matrix run and archive its evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=Path)
out = parser.parse_args().archive.resolve()
report_path = out / 'report.json'
report = json.loads(report_path.read_text())
for name, digest in report['archive_sha256'].items():
    assert hashlib.sha256((out / name).read_bytes()).hexdigest() == digest, name
run = json.loads((out / 'native-old/run.json').read_text())
assert run['result'] == run['application_result'] == 'PASS'
assert run['unmapped_access_count'] == 0 and run['emulator_log_fresh']
assert run['requested_3ds_model'] == 'old' and not run['model_verification_failure']
assert run['temporary_settings_restored']
assert run['original_config_sha256'] == run['restored_config_sha256']
assert run['sha256'] == report['archive_sha256']['test_fixed_direction_bounded_vectors.3dsx']
text = (out / 'native-old/log.txt').read_text()
assert 'CPU mode: model_query=00000000 new3ds=0 speedup_requested=0' in text
native_vectors = next(line for line in text.splitlines() if line.startswith('PASS vectors='))
host_counts = dict(re.findall(r'(\w+)=(\d+)', report['vectors']))
native_counts = dict(re.findall(r'(\w+)=(\d+)', native_vectors))
assert native_counts['vectors'] == host_counts['vectors']
assert native_counts['checksum'] == host_counts['checksum']
if 'matrix_bits_checksum' in host_counts:
    assert native_counts == host_counts, (native_counts, host_counts)

summary_pattern = re.compile(
    r'^Bounded direction microbench workload=(\S+) share=(\d+) .*'
    r'current_cached64=([\d.]+) candidate=([\d.]+) checksum=(\d+)$', re.M)
sample_pattern = re.compile(
    r'^Sample workload=(\S+) share=(\d+) round=(\d+) order=(\S+) '
    r'current_ms/vector=([\d.]+) candidate_ms/vector=([\d.]+) checksum=(\d+)$', re.M)
host_workloads = {(m[0], m[1]): m[4] for m in
                  summary_pattern.findall((out / 'host-vectors.log').read_text())}
measurements = []
for workload, share, current, candidate, checksum in summary_pattern.findall(text):
    samples = [m for m in sample_pattern.findall(text) if m[0:2] == (workload, share)]
    assert len(samples) == 4 and {int(m[2]) for m in samples} == set(range(4))
    assert all(m[3] == ('BA' if int(m[2]) & 1 else 'AB') for m in samples)
    assert all(m[6] == checksum for m in samples)
    assert checksum == host_workloads[(workload, share)]
    current, candidate = float(current), float(candidate)
    assert abs(sum(float(m[4]) for m in samples) / 4 - current) < 2e-9
    assert abs(sum(float(m[5]) for m in samples) / 4 - candidate) < 2e-9
    measurements.append({
        'workload': workload, 'share': int(share), 'vectors_per_round': 60000,
        'rounds': 4, 'current_cached64_us_per_vector': current * 1000,
        'bounded_cached32_us_per_vector': candidate * 1000,
        'gain_percent': (current - candidate) / current * 100,
        'checksum': int(checksum),
        'samples_ms_per_vector': [{'round': int(m[2]), 'order': m[3],
                                  'current': float(m[4]), 'candidate': float(m[5])}
                                 for m in samples],
    })
assert len(measurements) == 6

asm = (out / 'native-disassembly.log').read_text()
def function(name):
    pattern = r'^([0-9a-f]+) <' + name + r'[^\n]*>:\n(.*?)(?=^[0-9a-f]+ <|\Z)'
    match = re.search(pattern, asm, re.M | re.S)
    assert match, name
    return match[1], match[2]
candidate_address, candidate_asm = function('transform_candidate')
current_address, current_asm = function('transform_current')
assert re.search(r'\bsmull\b', candidate_asm)
assert re.search(r'\bsmlal\b', candidate_asm)
assert 'vcvt.s32.f32' in candidate_asm and '__aeabi_d2lz' in candidate_asm
assert '__aeabi_d2lz' in current_asm and re.search(r'\bumull\b', current_asm)
assert not re.search(r'\bsmull\b', current_asm)
shutil.copy2(Path(__file__), out / Path(__file__).name)
report['status'] = 'PASS host ASan/UBSan vectors/oracles and ARM11 classic vectors/microbench; production unchanged'
report['native'] = {
    'status': 'PASS', 'runner': run, 'vectors': native_vectors,
    'cache_counters_match_host': native_counts == host_counts,
    'shared_objects_used': False, 'measurements': measurements,
}
report['arm11_asm'] = {
    'candidate_address': candidate_address, 'current_address': current_address,
    'candidate_bounded': 'VFP float32/int32 conversions; SMULL then two SMLAL per axis (three axes). Full-bit 64-byte cache identity remains.',
    'candidate_fallback': 'Original double/int64 coefficient conversion and long multiplication retained.',
    'current': 'Double/int64 conversion on cache miss; MUL/MLA/UMULL long products.',
}
report['domain'] = {
    'condition': 'All nine matrix[k*4+j]*4096.f values are >= -2147483648.f and < 2147483648.f.',
    'float_matrix_interval': '[-524288.f, 524288.f); final half-away rounding exactly matches legacy within this domain.',
    'signed10_interval': [-512, 511],
    'bounded_absolute_sum_upper_bound': 3 * 512 * (1 << 31),
    'bounded_absolute_shifted_sum_upper_bound': 3 * 512 * (1 << 19),
    'fallback': 'If any of nine coefficients is outside the domain, all nine use legacy double/int64 conversion. NaN/Inf in unused seven words change cache identity only.',
    'legacy_undefined': 'Consumed NaN/Inf, int64 overflow, accumulation overflow or signed negation overflow are excluded from execution; their legacy fallback semantics are unchanged.',
}
report['timing_scope'] = 'ARM11 guest timings on Azahar classic, four AB/BA rounds per workload, no fine timers in hot paths. No physical-device, menu, audio, presentation, VBlank or FPS gain claim. Fallback regressions are retained.'
report['archive_sha256'] = {
    str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(out.rglob('*')) if p.is_file() and p.name != 'report.json'
}
report_path.write_text(json.dumps(report, indent=2) + '\n')
print(report['status'])
print('cache_counters_match_host:', report['native']['cache_counters_match_host'])
for m in measurements:
    print(f"{m['workload']} share={m['share']}: "
          f"{m['current_cached64_us_per_vector']:.6f} -> "
          f"{m['bounded_cached32_us_per_vector']:.6f} us, "
          f"gain {m['gain_percent']:.2f}%")
