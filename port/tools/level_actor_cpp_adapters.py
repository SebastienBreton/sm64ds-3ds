"""Bounded, reviewed compile adapters for native level actor source copies.

These do not establish actor ABI or gameplay correctness. They preserve the
locations of assignments/calls and never edit the matched source or headers.
"""

import re


def replace_once(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after, 1)


def adapt(source, text):
    changes = []
    # Some reviewed SDK sources use vuN aliases that hostgen's builtin type
    # parser does not recognize. Preserve the original transaction width.
    text, count = re.subn(r'\*\s*\(vu(8|16|32|64)\s*\*\s*\)\s*(0x0?4[0-9a-fA-F]+)\b',
                         lambda m: f'NTR_MMIO(uint{m[1]}_t, {m[2]})', text)
    if count:
        changes.append(f'Native bus for {count} original vuN MMIO lvalues')
    if source == 'src/actors/daBombking_c.cpp':
        text = replace_once(text, '#include "common.h"\n#include "daBombking_c.h"',
            '#include "common.h"\n#include "decl_common.h"\n#include "daBombking_c.h"')
        # The earlier C-linkage declaration makes the header's redeclaration
        # inherit that linkage. Header layout and vtable use are unchanged.
        changes.append('Declare existing C-linkage vtable symbol before header redeclaration')
        text = replace_once(text,
            '    daBombking_c *k = (daBombking_c *)thiz;\n    if (k->mWithMeshClsn.IsOnGround() == 0) goto done;',
            '    daBombking_c *k = (daBombking_c *)thiz;\n    Player *other;\n    short msg;\n    if (k->mWithMeshClsn.IsOnGround() == 0) goto done;')
        text = replace_once(text, '    Player *other = k->mTalkingPlayer;', '    other = k->mTalkingPlayer;')
        text = replace_once(text, '    short msg = 0;', '    msg = 0;')
        changes.append('Hoist POD declarations only; retain assignments after original guards')
    elif source == 'src/actors/daKrb_c.cpp':
        text = replace_once(text,
            '    int deathState = goomba->UpdateDeath(goomba->mWithMeshClsn);',
            '    unsigned int id;\n    int deathState = goomba->UpdateDeath(goomba->mWithMeshClsn);')
        text = replace_once(text, '    unsigned int id = goomba->mdCcAc_c.otherOwner;',
            '    id = goomba->mdCcAc_c.otherOwner;')
        changes.append('Hoist POD id declaration only; retain its guarded read')
    elif source == 'src/game/actors/d_a_wanwan.cpp':
        for declaration in (
            '    int ApproachAngle(void *angles, s32 a, s32 b, s32 c, s32 d);\n',
            '    u16 DecIfAbove0_Short(void *p);\n',
            '    void _ZN9ModelAnim7SetAnimEP8BCA_Filei5Fix12IiEj(void *self_, void *bca, s32 a, s32 fix, unsigned b);\n',
            '    void _ZN6Camera9SetFlag_3Ev(void *cam);\n'):
            text = replace_once(text, declaration, '')
        text = replace_once(text, '        ApproachAngle(ap, z, 4, 0x200, fifth);',
            '        ApproachAngle((short *)ap, z, 4, 0x200, fifth);')
        changes.append('Use existing typed C-linkage declarations; pointer cast preserves argument bits')
        # The only landing caller overwrites r0 immediately. The two update
        # targets are invoked exclusively by a void PMF reader, whose only
        # caller also overwrites r0. ROM assertions live in the independent
        # prove_wanwan_return_contract.py gate. No observed result is invented.
        text = replace_once(text, 'extern int func_ov014_02111fb8(char *c);',
            'extern void func_ov014_02111fb8(char *c);')
        for address in ('02111fb8','02111af0','0211150c'):
            text = replace_once(text, f'extern "C" int func_ov014_{address}(char *raw)',
                f'extern "C" void func_ov014_{address}(char *raw)')
        text = replace_once(text, '    if (r)\n        return r;', '    if (r)\n        return;')
        changes.append('ROM-proven ignored returns become void; guarded early exit preserved')
        text = replace_once(text, 'int func_0201267c(int id, void *pos);',
            'void func_0201267c(unsigned int id, const Vector3 *pos);')
        text = replace_once(text,
            'namespace call3_267c {\nextern "C" int func_0201267c(int id, void *pos, int unused);\n}', '')
        for before,after in (
            ('    func_0201267c(0x39, &c->mCamSpacePosX);',
             '    func_0201267c(0x39, (const Vector3 *)&c->mCamSpacePosX);'),
            ('    call3_267c::func_0201267c(0x3a, &c->mCamSpacePosX, 0);',
             '    func_0201267c(0x3a, (const Vector3 *)&c->mCamSpacePosX);'),
            ('    func_0201267c(0x3a, &c->mCamSpacePosX);',
             '    func_0201267c(0x3a, (const Vector3 *)&c->mCamSpacePosX);')):
            text=replace_once(text,before,after)
        changes.append('Original two-argument sound ABI; ROM overwrites r2 before use')
    elif source == 'src/d_s_star_sel.cpp':
        # Data aliases in the retail reconstruction denote GCC-owned class
        # vtables. The common address-point adapter applies their +2 prefix.
        for old, new in [('data_0208e4b8', '_ZTV7dBase_c'),
                         ('data_ov003_020b1704', '_ZTV12dScStarSel_c')]:
            assert text.count(old) == 2, old
            text = text.replace(old, new)
        changes.append('Bind retail factory vtable aliases to native class symbols')
    elif source == 'src/_ZN12dScStarSel_c13InitResourcesEv.cpp':
        for bits in (16, 32):
            text = replace_once(text, f'#define REG{bits}(a) (*(vu{bits} *)(a))',
                                f'#define REG{bits}(a) NTR_MMIO(uint{bits}_t, (a))')
        changes.append('Original MMIO macro transactions through native bus')
    elif source in ('src/_ZN7Message28DisplayStarNameForStarSelectEj.cpp',
                    'src/_ZN7Message30DisplayCourseNameForStarSelectEj.cpp'):
        # Original computed tilemap halfword stores retain order/address/value.
        # The physical bus, rather than an ARM11 DS pointer dereference, owns
        # VRAM aliasing and mirroring.
        text, count = re.subn(r'q\[(i|div|div2)\] = \(u16\)\((\1) \+ (0x[0-9a-f]+)\);',
            lambda m: f'NTR_MMIO(u16, uint32_t(uintptr_t(q + {m[1]}))) = (u16)({m[2]} + {m[3]});', text)
        assert count == (1 if 'Message28' in source else 2)
        changes.append('Original tilemap stores through native physical bus')
    elif source == 'src/game/actors/d_a_obj_kb1_billboard.cpp':
        # Storage is the imported overlay BSS; the retail static initializer
        # runs at overlay load (unmatched/levels_sinit_ov044.c), not at boot.
        text = replace_once(text, 'Kb1BillboardModelFilePtr data_ov044_02111680(1570);', '')
        changes.append('Overlay-load static initializer instead of GCC boot-time constructor')
    elif source == 'src/actors/daKuriKing_c.cpp':
        # Multi-module relocation: only level 45 (overlay 53) loads bank 74;
        # the decompiler spelled its level data with an ov010 label.
        for addr in ('021123f4', '021124ac'):
            text = text.replace('data_ov010_' + addr, 'data_ov053_' + addr)
        changes.append('Bind level data to its only provider overlay 53')
    elif source in ('src/actors/daOnms_c.cpp', 'src/actors/daTor_c.cpp'):
        # C tentative definition spelling; the ARM9 sine table is external.
        text = replace_once(text, '\ns16 data_02082214[];', '\nextern s16 data_02082214[];')
        changes.append('Declare the ARM9 table instead of an incomplete C++ definition')
    elif source in ('src/actors/daSanbo_c.cpp', 'src/actors/daYurei_Mucho_c.cpp'):
        header = '#include "daSanbo_c.h"' if 'Sanbo' in source else '#include "daYurei_Mucho_c.h"'
        text = replace_once(text, header, '#include "decl_common.h"\n' + header)
        changes.append('Declare existing C-linkage vtable symbol before header redeclaration')
    elif source == 'src/actors/daPopoi_c.cpp':
        old = ('extern "C" {\n// @symbol daPopoi_c_classInit\n#include "decl_ActorBase.h"\n#include "decl_Enemy.h"\n'
               '#include "decl_ModelAnim.h"\n#include "decl_dCcAc_c.h"\n#include "decl_dCcAcPos_c.h"\n'
               '#include "decl_ShadowModel.h"\n#include "decl_dBgCh_Actr.h"\n#include "decl_common.h"\n'
               '#include "daPopoi_c.h"\n#include "Model.h"\n#include "Player.h"\n#include "Animation.h"\n'
               '#include "SurfaceInfo.h"\n#include "dBgCh_Lin.h"\n')
        new = ('extern "C" {\n#include "decl_ActorBase.h"\n#include "decl_Enemy.h"\n'
               '#include "decl_ModelAnim.h"\n#include "decl_dCcAc_c.h"\n#include "decl_dCcAcPos_c.h"\n'
               '#include "decl_ShadowModel.h"\n#include "decl_dBgCh_Actr.h"\n#include "decl_common.h"\n}\n'
               '#include "daPopoi_c.h"\n#include "Model.h"\n#include "Player.h"\n#include "Animation.h"\n'
               '#include "SurfaceInfo.h"\n#include "dBgCh_Lin.h"\nextern "C" {\n// @symbol daPopoi_c_classInit\n')
        text = replace_once(text, old, new)
        changes.append('C++ headers outside the C-linkage declaration block')
    elif source == 'src/game/actors/d_a_propeller_heyho.cpp':
        for name in ('data_ov070_0212359c', 'data_ov070_021235cc'):
            decl = f'extern daPropeller_Heyho_c::State {name};\n'
            assert text.count(decl) >= 2, name
            first = text.index(decl) + len(decl)
            text = text[:first] + text[first:].replace(decl, '')
        changes.append('Drop C-linkage redeclarations of C++-linkage state tables')
    elif source == 'src/game/actors/d_a_obj_fm_battan.cpp':
        text = replace_once(text, 'BattanModelFilePtr data_ov023_02112088(1558);', '')
        text = replace_once(text, 'BattanCollisionFilePtr data_ov023_02112080(1559);', '')
        changes.append('Overlay-load static initializer (src/__sinit_ov023_02111aa8.c) instead of GCC boot constructor')
    elif source == 'src/game/actors/d_a_obj_rc_carpet.cpp':
        # The ROM data label is dPathLiftActor_c's vtable address point
        # (ov002 0x0210af70 == _ZTV16dPathLiftActor_c); GCC's point is +8.
        text = replace_once(text, '*(int *)actor = (int)data_ov002_0210af70;',
                            '*(int *)actor = (int)((char *)_ZTV16dPathLiftActor_c + 8);')
        text = replace_once(text, 'extern int data_ov002_0210af70[];',
                            'extern int data_ov002_0210af70[];\nextern "C" int _ZTV16dPathLiftActor_c[];')
        changes.append('Native GCC vtable address point for the inlined dPathLiftActor_c vptr store')
    # Non-void functions that flow off their end (ROM tail calls / dead r0).
    # GCC treats that as unreachable and lays out garbage control flow, so the
    # native copy returns the tail call's result where the ROM does, else 0.
    returns = {
        'src/actors/daDonketu_c.cpp': [('    mFileTable = (int)&data_ov064_0211b834;\n    InitResourcesCommon();\n}',
                                        '    mFileTable = (int)&data_ov064_0211b834;\n    return InitResourcesCommon();\n}')],
        'src/actors/daObjFl_Ukiyuka_c.cpp': [('    func_ov002_020b6584(this, &data_ov022_021140d4[mVariant], kBobAmplitude);\n}',
                                             '    return func_ov002_020b6584(this, &data_ov022_021140d4[mVariant], kBobAmplitude);\n}')],
        'src/actors/daKuriKing_c.cpp': [('    ApproachAngle(&c->mAngleY, (short)((c->mDirection * 0x4500) + c->mWalkAngle), 0xa, 0x800, 0x10);\n}',
                                         '    return ApproachAngle(&c->mAngleY, (short)((c->mDirection * 0x4500) + c->mWalkAngle), 0xa, 0x800, 0x10);\n}')],
        'src/actors/daOts_c.cpp': [('int daOts_c::UpdateRunState()\n{\n}', 'int daOts_c::UpdateRunState()\n{\n    return 0;\n}')],
        'src/actors/daObjBk_Botaosi_c.cpp': [('int daObjBk_Botaosi_c::OnAttacked2(dActor_c &other)\n{\n    func_ov015_02111414(this, &other);\n}',
                                              'int daObjBk_Botaosi_c::OnAttacked2(dActor_c &other)\n{\n    func_ov015_02111414(this, &other);\n    return 0;\n}')],
        'src/actors/daObjBk_Kabe_c.cpp': [('int daObjBk_Kabe_c::OnHitByCannonBlastedChar(dActor_c &other)\n{\n    Kill();\n}',
                                           'int daObjBk_Kabe_c::OnHitByCannonBlastedChar(dActor_c &other)\n{\n    Kill();\n    return 0;\n}')],
        'src/actors/daObjBk_Lift_c.cpp': [('    mJustSteppedOn = 1;\n    mMove = 1;\n}', '    mJustSteppedOn = 1;\n    mMove = 1;\n    return 1;\n}')],
        'src/game/actors/d_a_propeller_heyho.cpp': [('    c->KillAndTrackInDeathTable();\n}', '    c->KillAndTrackInDeathTable();\n    return 0;\n}')],
        'src/game/actors/d_a_obj_hm_bskt.cpp': [('    ((fBase_c *)c)->MarkForDestruction();\n}', '    ((fBase_c *)c)->MarkForDestruction();\n    return 0;\n}')],
        'src/game/actors/daObjPushblock_c.cpp': [('        else mHorzSpeed = 0x4000;\n    }\n}', '        else mHorzSpeed = 0x4000;\n    }\n    return 0;\n}')],
    }
    for before, after in returns.get(source, []):
        if source == 'src/game/actors/d_a_propeller_heyho.cpp':
            assert text.count(before) >= 1, before
            text = text.replace(before, after)
        else:
            text = replace_once(text, before, after)
    if source in returns:
        changes.append('Defined return value where the ROM tail-calls or leaves r0 unused')
    if source in ('src/actors/daGmch_c.cpp', 'src/game/actors/d_a_pg_rcer.cpp'):
        before = '        ((dBgPi *)_ZNK10dBgCh_Actr13GetWallResultEv(clsn))->surface.CopyNormalTo(*(Vector3 *)wallNormal);\n    }\n}'
        text = replace_once(text, before, before[:-1] + '    return 0;\n}')
        changes.append('Defined return value (ROM r0 unused by callers)')
    return text, changes
