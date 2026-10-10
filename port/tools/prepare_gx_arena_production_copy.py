#!/usr/bin/env python3
"""Mechanically relocate the proved GX arena into a private production layout."""
import argparse, hashlib, json, shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT/'port'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert not out.exists(), 'Preserve earlier copies and their proofs'
    source = ROOT/'build3ds/validation/gx-arena-production-plan/frozen'
    manifest = json.loads((source.parent/'manifest.json').read_text())
    assert all(sha(source/n)==h for n,h in manifest['frozen_sha256'].items())
    assert all(sha(Path(v['production_path']))==v['production_sha256'] for v in manifest['sources'])
    layout = out/'port'
    includes = {
        '"gx_geometry_arena_candidate.h"':'"platform/3ds/gx_geometry_arena.h"',
        '"gx_arena_release_builder.h"':'"platform/3ds/gx_geometry_arena.h"',
        '"gx_arena_resources_candidate.h"':'"platform/3ds/gx_geometry_resources.h"',
        '"gx_direct_pipeline_api.h"':'"ntr/gx_direct.h"',
        '"geometry_arena_viewer.h"':'"platform/3ds/geometry_arena_viewer.h"',
    }

    def names(code):
        code = code.replace('gx_arena_candidate','gx_geometry').replace('ReleaseBuilder','Builder')
        for before, after in includes.items():
            code = code.replace(before,after)
        return code

    def write(name,code):
        path = layout/name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(code)

    old = (source/'gx_geometry_arena_candidate.h').read_text()
    packet = old[old.index('struct Material {'):old.index('// Caller owns capacities')]
    types = old[old.index('struct Arena {'):old.index('class Builder {')]
    old_builder = (source/'gx_arena_release_builder.h').read_text()
    builder = old_builder[old_builder.index('class ReleaseBuilder {'):old_builder.rfind('\n}')]
    write('ntr/include/ntr/gx_geometry_packet.h',
        '#pragma once\n// Original GX vertices and ordered frame-local indexed geometry. No GPU ownership.\n'
        '#include "ntr/gx.h"\n#include "ntr/host_allocator.h"\n#include <cstdint>\n'
        'namespace ntr::gx_geometry {\n'+packet+'}\n')
    write('platform/3ds/gx_geometry_arena.h',
        '#pragma once\n// Caller must acquire/drain GPU readers before any arena or batch write.\n'
        '#include "ntr/gx_geometry_packet.h"\n#include "platform/3ds/ds_gpu_viewport.h"\n'
        '#include <algorithm>\nnamespace ntr::gx_geometry {\n'+types+names(builder)+'\n}\n')
    write('platform/3ds/gx_geometry_resources.h',names((source/'gx_arena_resources_candidate.h').read_text()))
    api = names((source/'gx_direct_pipeline_api.h').read_text())
    api = api.replace('#include "platform/3ds/gx_geometry_arena.h"','#include "ntr/gx_geometry_packet.h"')
    api = api.replace('// Isolated, fixed for the whole process before original game setup.',
        '// Configure once before the first GX frame; never switch a live primitive or lease.')
    api = api.replace('void player_direct_configure(bool reuse_between_presentations);\n','')
    write('ntr/include/ntr/gx_direct.h',api)
    viewer_api = names((source/'geometry_arena_viewer.h').read_text())
    viewer_api = viewer_api.replace('// Isolated viewer lease API.', '// Native viewer lease API.')
    viewer_api = viewer_api.replace('// Ends an open command list and invalidates this token. Does not free memory.',
        '// Ends an open command list and invalidates this token. Does not retire submitted\n'
        '// GPU readers or free memory: mutate/destroy resources only after acquire or exit drain.')
    write('platform/3ds/geometry_arena_viewer.h',viewer_api)
    gx = names((source/'gx.cpp').read_text())
    gx = gx.replace('#include "platform/3ds/gx_geometry_arena.h"','#include "ntr/gx_geometry_packet.h"')
    write('ntr/gx.cpp',gx)
    driver = names((source/'player_present.cpp').read_text())
    driver = driver.replace('#include "platform/3ds/gx_geometry_arena.h"\n#include "platform/3ds/gx_geometry_resources.h"',
        '#include "platform/3ds/gx_geometry_resources.h"',1)
    write('platform/3ds/player_present.cpp',driver)
    write('platform/3ds/viewer_3ds.cpp',names((source/'viewer_3ds.cpp').read_text()))
    startup = '''#pragma once
// Read geometry policy once before original game/GX setup.
#include "ntr/gx_direct.h"
#include <cstdlib>
#include <cstring>
void player_direct_configure(bool reuse_between_presentations);
namespace player_geometry {
inline bool enabled(const char* value){return !value || std::strcmp(value,"0")!=0;}
inline void startup(){
    const bool direct=enabled(std::getenv("PLAYER_DIRECT_ARENA"));
    const bool retained=direct && enabled(std::getenv("PLAYER_DIRECT_ARENA_REUSE"));
    ntr::gx_direct_configure(direct);
    player_direct_configure(retained);
}
}
'''
    write('platform/3ds/player_geometry.h',startup)
    entry = (PORT/'platform/3ds/game_main.cpp').read_text()
    entry = entry.replace('#include "ntr/gx.h"','#include "ntr/gx.h"\n#include "platform/3ds/player_geometry.h"',1)
    assert entry.count('int main(){')==1
    entry = entry.replace('int main(){','int main(){\n player_geometry::startup();',1)
    write('platform/3ds/game_main.cpp',entry)
    out.mkdir(exist_ok=True)
    shutil.copy2(Path(__file__),out/Path(__file__).name)
    result = {'status':'PREPARED mechanical production-layout copy; proof NOT_EXECUTED',
        'production_modified':False,'source_manifest_sha256':sha(source.parent/'manifest.json'),
        'namespace':'ntr::gx_geometry','default_policy':'Native game startup arena+geometry retained on, literal0 disabled; generic ntr legacy until configured',
        'arithmetic_changes':'None; Packet/types/ReleaseBuilder bodies split verbatim, namespace/class/include names only',
        'excluded':['PPU epochs','OBJ epochs','composition cache','pacing changes','immutable GX-list IR'],
        'baseline_sha256':{v['production_path']:v['production_sha256'] for v in manifest['sources']},
        'game_main_baseline_sha256':sha(PORT/'platform/3ds/game_main.cpp'),
        'files_sha256':{str(p.relative_to(out)):sha(p) for p in layout.rglob('*') if p.is_file()}}
    (out/'prepare.json').write_text(json.dumps(result,indent=2)+'\n')
    print(result['status']);print(out)


if __name__=='__main__':
    main()
