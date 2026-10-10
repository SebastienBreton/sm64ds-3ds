#!/usr/bin/env python3
"""Freeze a menu diagnostic that measures PPU preparation on the resident path.

The production driver is unchanged. This diagnostic includes timing overhead
and must not be used as an optimization A/B benchmark.
"""
from pathlib import Path
import hashlib
import json
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / 'port'
SHARED = ROOT / 'build3ds/cmake'
OUT = ROOT / 'build3ds/validation/resident-ppu-profile'
GCC = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-g++'
OUT.mkdir(parents=True, exist_ok=True)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def flags(target):
    content = (SHARED / f'CMakeFiles/{target}.dir/flags.make').read_text()
    return [token for key in ('CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS')
            for token in shlex.split(re.search(r'^'+key+r' = (.*)$', content, re.M)[1])]


def run(command, name, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    (OUT / (name+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()


source = (PORT/'platform/3ds/player_present.cpp').read_text()
(OUT/'player-present-original.cpp').write_text(source)
anchor = '            const auto a=prepare(ntr::ENGINE_A),b=prepare(ntr::ENGINE_B);'
assert source.count(anchor) == 1
replacement = '''            ntr::ppu_prepare_profile_clock(v3ds_time_ms);
            const auto a=prepare(ntr::ENGINE_A),b=prepare(ntr::ENGINE_B);
            static unsigned diagnostic_frames=0;
            if(++diagnostic_frames%60==0){
                for(auto engine:{ntr::ENGINE_A,ntr::ENGINE_B}){
                    const auto profile=ntr::ppu_prepare_profile_read(engine,true);
                    std::printf("Resident PPU preparation engine=%u calls=%llu total_ms windows=%.6f bg=%.6f objects=%.6f resolve=%.6f\\n",
                        unsigned(engine),(unsigned long long)profile.calls,
                        profile.windows,profile.bg,profile.objects,profile.resolve);
                }
                uint64_t hits,misses;
                ntr::ppu_prepared_cache_stats(hits,misses);
                std::printf("Resident prepared cache cumulative hits=%llu misses=%llu\\n",
                    (unsigned long long)hits,(unsigned long long)misses);
                ntr::ppu_background_cache_stats(hits,misses);
                std::printf("Resident background cache cumulative hits=%llu misses=%llu\\n",
                    (unsigned long long)hits,(unsigned long long)misses);
            }'''
diagnostic = source.replace(anchor,replacement)
driver = OUT/'player-present-profile.cpp'
driver.write_text(diagnostic)
config = SHARED/'.dkp-generated/presentation/gpu_composite_build_config.h'
shutil.copy2(config,OUT/config.name)
assert '#define SM64DS_NATIVE_OBJ_CANDIDATE' in config.read_text()
run([GCC,*flags('smoke_player'),'-DSM64DS_GPU_COMPOSITE_CONFIG',
     '-I'+str(OUT),'-I'+str(PORT/'platform/3ds'),'-c',str(driver),
     '-o',str(OUT/'driver.o')],'compile-driver')
probe = OUT/'fps_menu_resident_ppu_profile.cpp'
shutil.copy2(PORT/'tests/fps_menu_game_profile.cpp',probe)
run([GCC,*flags('smoke_player'),'-c',str(probe),'-o',str(OUT/'probe.o')],
    'compile-probe')
command = shlex.split((SHARED/'CMakeFiles/smoke_player.dir/link.txt').read_text())
for original,replacement in [
    ('CMakeFiles/smoke_player.dir/tests/smoke_player.o',OUT/'probe.o'),
    ('CMakeFiles/smoke_player.dir/platform/3ds/player_present.o',OUT/'driver.o')]:
    assert command.count(original)==1
    command[command.index(original)] = str(replacement)
elf = probe.with_suffix('.elf')
binary = elf.with_suffix('.3dsx')
command[command.index('-o')+1]=str(elf)
command=[str(SHARED/t) if not t.startswith('-') and (SHARED/t).is_file() else t for t in command]
boot=(SHARED/'CMakeFiles/sm64ds_sound_boot.dir/build.make').read_text()
extra=[SHARED/m[1] for m in re.finditer(r'^sm64ds_sound_boot: (.+\\.o)$',boot,re.M)]
existing={Path(t).resolve() for t in command if t.endswith('.o')}
command[1:1]=[str(p) for p in extra if p.resolve() not in existing]
inputs={Path(t) for t in command if t.endswith(('.o','.a','.ld'))}
hashes={str(p):digest(p) for p in inputs}
run(command,'link',cwd=OUT)
assert hashes=={str(p):digest(p) for p in inputs}
run(['/opt/devkitpro/tools/bin/3dsxtool',str(elf),str(binary)],'3dsx')
(OUT/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
(OUT/'link-command.json').write_text(json.dumps(command,indent=2)+'\n')
(OUT/'build.json').write_text(json.dumps({
    'status':'PASS ARM11 link; runtime NOT_EXECUTED',
    'production_changed':False,'diagnostic_timing_overhead':True,
    'source_sha256':digest(PORT/'platform/3ds/player_present.cpp'),
    'diagnostic_sha256':digest(driver),'binary_sha256':digest(binary),
    'link_input_count':len(inputs),
    'scope':'Original menu, per-engine resident PPU preparation; not a performance A/B',
},indent=2)+'\n')
print(binary)
