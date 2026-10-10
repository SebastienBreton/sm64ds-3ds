#!/usr/bin/env python3
"""ROM-checked ov2 profiles whose factories are enrolled in production objects.

The manifest is reviewed from the production link object's symbol inventory;
factory presence in the final ELF is insufficient because section GC removes
unregistered factories. No missing factory or actor body is synthesized here.
"""
import argparse,struct
from pathlib import Path
# name, original factory, ROM address, complete original profile words.
ROWS = [
    ('ONEUPKINOKO', 'da1up_c_classInit_ONEUPKINOKO', 34636680, (34276736, 9175316, 3, 204800, 1228800, 16777216, 16777216)),
    ('SCALEUP_KINOKO', 'da1up_c_classInit_SCALEUP_KINOKO', 34636708, (34276656, 9240853, 3, 204800, 1228800, 16777216, 16777216)),
    ('CH_SCENE', 'daChScene_c_classInit', 34637456, (34279204, 22872413, 0, 0, 0, 0, 0)),
    ('COIN', 'daCoin_c_classInit_COIN', 34637712, (34286408, 10092832, 536870914, 163840, 163840, 16777216, 12288000)),
    ('RED_COIN', 'daCoin_c_classInit_RED_COIN', 34637740, (34286320, 10158369, 134217730, 163840, 163840, 25165824, 12288000)),
    ('BLUE_COIN', 'daCoin_c_classInit_BLUE_COIN', 34637768, (34286232, 10223906, 2, 163840, 163840, 16777216, 12288000)),
    ('BLOCK_L', 'daObjBlockL_c_classInit_BLOCK_L', 34638372, (34292012, 2097167, 2, 409600, 819200, 25165824, 25165824)),
    ('DP_BLOCK_L', 'daObjBlockL_c_classInit_DP_BLOCK_L', 34638400, (34291964, 2162704, 2, 614400, 1228800, 67108864, 67108864)),
    ('SW_BLOCK_L', 'daObjBlockL_c_classInit_SW_BLOCK_L', 34638428, (34291916, 2359315, 2, 409600, 819200, 25165824, 25165824)),
    ('BLOCK_LL', 'daObjBlockL_c_classInit_BLOCK_LL', 34638456, (34291868, 2228241, 2, 614400, 1228800, 25165824, 25165824)),
    ('BK_FUTA', 'daObjBlockL_c_classInit_BK_FUTA', 34638484, (34291820, 13041710, 2, 409600, 819200, 25165824, 25165824)),
    ('WATERFALL', 'daObjWaterfall_c_classInit', 34641020, (34303720, 1376453, 8388611, 393216, 3145728, 33554432, 0)),
    ('TATEFUDA', 'daObjTatefuda_c_classInit', 34642644, (34325448, 10944696, 130, 262144, 524288, 16777216, 16777216)),
    ('PLAYER', 'daPly_c_classInit', 34645764, (34499596, 1179839, 2, 1024000, 1024000, 49152000, 49152000)),
    ('TREE', 'daTree_c_classInit', 34647004, (34521900, 9961758, 0, 0, 0, 0, 0)),
    ('YOSHI_EGG', 'daYegg_c_classInit', 34647440, (34530264, 10289161, 2, 204800, 286720, 16777216, 16777216)),
    ('SECRET_COIN', 'daSCoin_c_classInit', 34648076, (34539612, 21627209, 0, 0, 3276800, 32768000, 327680)),
    ('OBJ_NUMBER', 'daObjNumber_c_classInit', 34648264, (34540944, 21758282, 75497472, 0, 40960, 0, 0)),
    ('OBJ_1UPLOGO', 'daObj1UpLogo_c_classInit', 34648456, (34541936, 21823819, 75497472, 0, 40960, 0, 0)),
    ('SET_SE', 'daSetSE_c_classInit', 34649212, (34544532, 22937950, 1686110208, 0, 4096, 0, 0)),
    ('MUGEN_BGM', 'daMugenBGM_c_classInit', 34649440, (34545472, 23003487, 0, 0, 0, 0, 0)),
    ('DEMO', 'daDemo_c_classInit', 34650720, (34572296, 23069024, 1686110210, 4096000, 4096000, 40960000, 40960000)),
    ('MAP', 'dMap_c_classInit', 34652512, (34584764, 21954895)),
    ('METER', 'dMeter_c_classInit', 34652688, (34595156, 21889358)),
    ('HANSWITCH', 'daObjSwitch_c_classInit_HANSWITCH', 34642176, (34319068, 8847371, 2, 0, 1048576, 16777216, 0)),
    ('CANNON_SHUTTER', 'daObjCannonShutter_c_classInit', 34643220, (34327756, 10354702, 8388610, 0, 1048576, 16777216, 0)),
    ('STAR', 'daStar_c_classInit_STAR', 34646592, (34520880, 4194482, 536870914, 204800, 2048000, 16777216, 16777216)),
    ('STARBASE', 'daStarBase_c_classInit', 34646648, (34520716, 4325556, 671088642, 0, 819200, 16777216, 16777216)),
    ('HEART', 'daObjHeart_c_classInit', 34643024, (34326708, 11141417, 3, 393216, 393216, 16777216, 16777216)),
    ('OBJ_MARIO_CAP', 'daObjMarioCap_c_classInit', 34641356, (34311064, 6357261, 8388610, 204800, 204800, 16777216, 16777216)),
    ('SILVER_STAR', 'daStar_c_classInit_SILVER_STAR', 34646620, (34520788, 4260019, 671088642, 204800, 2048000, 18432000, 16777216)),
    ('BLK_SKINOKO_TAG', 'daObjBlockItemTag_c_classInit_BLK_SKINOKO_TAG', 34638752, (34293312, 20971842, 2, 393216, 2097152, 16777216, 0)),
    ('ENEMY_SWITCH', 'daESwitch_c_classInit', 34648868, (34543956, 23396709, 0, 0, 0, 0, 0)),
    ('ENEMY_CREATE', 'daECreate_c_classInit', 34648896, (34543908, 23462246, 0, 0, 0, 0, 0)),
    ('POWER_UP_ITEM', 'daObjPowerUpItem_c_classInit', 34641824, (34315788, 19202354, 2, 393216, 2097152, 16777216, 16777216)),
    ('FEATHER', 'daFeather_c_classInit', 34637956, (34288200, 22610265, 2, 393216, 2097152, 16777216, 16777216)),
    ('FPAKUN_BALL', 'daFPknBall_c_classInit', 34652016, (34575108, 7340286, 2, 327680, 327680, 16777216, 5242880)),
    ('SOUND_OBJ', 'daSoundObj_c_classInit', 34652260, (34576172, 23527783, 612368384, 0, 4096, 0, 0)),
    ('WARPKUN', 'daWarpkun_c_classInit', 34647192, (34522420, 22741339, 3, 0, 40960, 9830400, 0)),
]
# Original ov2 profiles required by admitted level banks 12/13/15/79.
ROWS += [
    ('BAR', 'daBar_c_classInit', 34636892, (34277136, 10027295, 3, 0, 0, 0, 0)),
    ('BLK_OKINOKO_TAG', 'daObjBlockItemTag_c_classInit_BLK_OKINOKO_TAG', 34638836, (34293360, 20906305, 2, 393216, 2097152, 16777216, 0)),
    ('OBJ_RED_FIRE', 'daObjFire_c_classInit_OBJ_RED_FIRE', 34639608, (34298336, 20578620, 8388611, 0, 1048576, 16777216, 0)),
    ('OBJ_BLUE_FIRE', 'daObjFire_c_classInit_OBJ_BLUE_FIRE', 34639636, (34298280, 20644157, 8388611, 0, 1048576, 33554432, 0)),
    ('STAR_SWITCH', 'daObjSwitch_c_classInit_STAR_SWITCH', 34642204, (34319020, 8912908, 2, 0, 1048576, 16777216, 0)),
    ('STAR_CAMERA', 'StarCamera_Spawn', 34646676, (34520668, 4128945, 0, 0, 819200, 0, 0)),
    ('BC_SWITCH', 'daObjBC_Switch_c_classInit', 34648648, (34543052, 21692426, 2, 819200, 819200, 16777216, 16777216)),
]
# Remaining original ov2 profiles (levels-render batch).
ROWS += [
    ('CAMERA_TAG', 'daCamTag_c_classInit', 34637080, (34277320, 65869, 0, 0, 0, 0, 0)),
    ('CH_ROOM', 'daChRoom_c_classInit', 34637268, (34277760, 22806876, 0, 0, 0, 0, 0)),
    ('OBJ_ABUKU', 'daObjAbuku_c_classInit', 34638144, (34289000, 10551587, 0, 393216, 2097152, 16777216, 0)),
    ('BLK_GNSHELL_TAG', 'daObjBlockItemTag_c_classInit_BLK_GNSHELL_TAG', 34638780, (34293264, 21037379, 2, 393216, 2097152, 16777216, 0)),
    ('BLK_SLVSTAR_TAG', 'daObjBlockItemTag_c_classInit_BLK_SLVSTAR_TAG', 34638808, (34293216, 21102916, 2, 393216, 2097152, 16777216, 0)),
    ('KINOKO_TAG', 'daObjKinokoTag_c_classInit_KINOKO_TAG', 34639028, (34294328, 20840768, 3, 393216, 2097152, 16777216, 0)),
    ('KINOKO_CREATE_TAG', 'daObjKinokoTag_c_classInit_KINOKO_CREATE_TAG', 34639056, (34294272, 20775231, 0, 0, 0, 0, 0)),
    ('HATENA_SWITCH', 'daObjHatenaSwitch_c_classInit', 34639416, (34297560, 19267610, 2, 1048576, 2097152, 16777216, 0)),
    ('LAVA', 'daObjLava_c_classInit', 34640828, (34303448, 1310916, 0, 0, 4096, 0, 0)),
    ('PUSHBLOCK', 'daObjPushblock_c_classInit', 34641548, (34312460, 2031808, 2, 614400, 1228800, 16777216, 0)),
    ('WAKAME', 'daObjWakame_c_classInit', 34642836, (34325928, 11075880, 3, 393216, 1376256, 33554432, 0)),
    ('PROPELLER_HEYHO_FIRE', 'daPropeller_Heyho_Fire_c_classInit', 34657840, (34598860, 5767401, 2, 409600, 409600, 16777216, 16777216)),
]
EXTERNAL = {'CANNON_SHUTTER','BAR','OBJ_RED_FIRE','OBJ_BLUE_FIRE'}
INITIALIZERS = ['__sinit_ov002_02100e50', '__sinit_ov002_02101064', '__sinit_ov002_021014e4', '__sinit_ov002_02107f88', '__sinit_ov002_02101588', '__sinit_ov002_02101900', '__sinit_ov002_02101968']
INITIALIZERS += ['__sinit_ov002_02107304']
# Remaining ov2 static initializers (levels-render batch): every ov2 ctor runs on DS.
INITIALIZERS += ['__sinit_ov002_02100938', '__sinit_ov002_02100ec4', '__sinit_ov002_02100f84', '__sinit_ov002_02101478', '__sinit_ov002_02101894', '__sinit_ov002_02108094']
LEVEL_NAMES = {'HANSWITCH','CANNON_SHUTTER','STAR','STARBASE','HEART','OBJ_MARIO_CAP','SILVER_STAR','BLK_SKINOKO_TAG','ENEMY_SWITCH','ENEMY_CREATE','POWER_UP_ITEM','FEATHER','FPAKUN_BALL','SOUND_OBJ','WARPKUN'}
LEVEL_NAMES.update({'BC_SWITCH', 'OBJ_BLUE_FIRE', 'STAR_SWITCH', 'BLK_OKINOKO_TAG', 'BAR', 'STAR_CAMERA', 'OBJ_RED_FIRE'})
LEVEL_NAMES.update({'HATENA_SWITCH', 'PROPELLER_HEYHO_FIRE', 'BLK_GNSHELL_TAG', 'LAVA', 'PUSHBLOCK', 'CH_ROOM', 'CAMERA_TAG', 'KINOKO_CREATE_TAG', 'KINOKO_TAG', 'OBJ_ABUKU', 'WAKAME', 'BLK_SLVSTAR_TAG'})
EXTERNAL.update({'HATENA_SWITCH', 'LAVA', 'CH_ROOM', 'KINOKO_TAG', 'OBJ_ABUKU', 'KINOKO_CREATE_TAG'})
PUBLIC = {'TREE','CH_SCENE','SET_SE','DEMO','MAP','METER'}
def generate(root):
 b=(root/'extracted/overlays/overlay_0002.bin').read_bytes();arm9=(root/'extracted/arm9_dec.bin').read_bytes()
 lines=['extern "C" {','struct Profile {void*(*create)();unsigned short behavior,render;unsigned flags;int range,height,draw,secondary;};','struct BaseProfile {void*(*create)();unsigned short behavior,render;};','static_assert(sizeof(Profile)==28,"profile ABI");','static_assert(sizeof(BaseProfile)==8,"base profile ABI");']
 names=[]
 for name,factory,a,expected in ROWS:
  v=struct.unpack_from('<'+str(len(expected))+'I',b,a-0x20ad660)
  if v!=expected or struct.unpack_from('<I',arm9,0x2090864-0x2004000+(v[1]&65535)*4)[0]!=a:raise ValueError('profile changed '+name)
  symbol=('g_profile_' if name in PUBLIC or name in EXTERNAL else 'sm64ds_common_profile_')+name
  names.append((symbol,factory,v,name in LEVEL_NAMES))
  kind='BaseProfile' if len(v)==2 else 'Profile'
  lines += [f'void* {factory}();']
  if name in EXTERNAL:
   lines += [f'extern {kind} {symbol};']
  else:
   lines += [f'{kind} {symbol}={{'+','.join([factory,str(v[1]&65535),str(v[1]>>16)]+[str(x) for x in v[2:]])+'};']
 lines += [f'void {init}();' for init in INITIALIZERS]
 lines += ['extern void** data_020a4bb8;', 'void __sinit_ov002_02100c50();', 'void __sinit_ov002_02107118();', 'unsigned sm64ds_check_coin_states();', 'unsigned sm64ds_check_yoshiegg_states();', 'void sm64ds_init_common_scenery_profiles(){', ' __sinit_ov002_02100c50(); __sinit_ov002_02107118();']
 for symbol,factory,v,level in names:
  if not level:lines.append(f' data_020a4bb8[{v[1]&65535}]=&{symbol};')
 lines += ['}', 'static bool level_actor_profiles_initialized=false;', 'void sm64ds_init_common_level_actor_profiles(){', ' if(level_actor_profiles_initialized)return;']
 lines += [f' {init}();' for init in INITIALIZERS]
 for symbol,factory,v,level in names:
  if level:lines.append(f' data_020a4bb8[{v[1]&65535}]=&{symbol};')
 lines += [' level_actor_profiles_initialized=true;']
 lines += ['}', 'unsigned sm64ds_check_common_profiles(){', ' if(sm64ds_check_coin_states()!=9 || sm64ds_check_yoshiegg_states()!=8)return 0;', ' unsigned count=0;']
 for symbol,factory,v,level in names:
  if level:lines += [' if(level_actor_profiles_initialized){']
  checks=[f'data_020a4bb8[{v[1]&65535}]!=&{symbol}',f'{symbol}.create!={factory}',f'{symbol}.behavior!={v[1]&65535}',f'{symbol}.render!={v[1]>>16}']
  checks += [f'(unsigned){symbol}.{field}!={value}u' for field,value in zip(['flags','range','height','draw','secondary'],v[2:])]
  lines.append(' if('+' || '.join(checks)+')return 0; ++count;')
  if level:lines += [' }']
 return '\n'.join(lines+[' return count;','}','}'])+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('output',type=Path)
 a=p.parse_args();a.output.write_text(generate(a.root))
