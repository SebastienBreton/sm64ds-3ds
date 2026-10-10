#!/usr/bin/env python3
"""Read original animation cursors and local/global bones from retail RAM.

No pose repair or animation advancement. Captures are labelled by source phase;
the displayed polygon phase must be verified separately before comparison.
"""
import argparse,json,struct,hashlib
from pathlib import Path


def inspect_ram(path,checkpoint=None,phase='ram_snapshot'):
    data=Path(path).read_bytes()
    if len(data)!=4*1024*1024:raise ValueError('Expected 4 MiB DS RAM')
    def read(address,fmt='I',count=1):
        offset=address-0x02000000;size=struct.calcsize(fmt)*count
        if offset<0 or offset+size>len(data):raise ValueError(f'Invalid RAM pointer {address:#x}')
        result=struct.unpack_from('<'+fmt*count,data,offset)
        return result[0] if count==1 else list(result)
    def bca(address):
        if not address:return None
        return {'header_words':read(address,'I',2),'table_relative_offsets':[read(address+o)-address for o in [8,12,16,20]]}
    models=[];player=read(0x0209f394)
    for slot in range(4):
        model=read(player+0xdc+slot*4)
        if not model:models.append(None);continue
        if read(model)!=0x0208e9b4:raise ValueError('Player body is not expected original ModelAnim2')
        file=read(model+8);count=read(file+4);bones=read(model+0x10);transforms=read(model+0x14)
        if not 0<count<=256:raise ValueError('Invalid body bone count')
        local=[]
        for index in range(count):
            bone=bones+index*0x34
            local.append({'scale_fx12':read(bone+0xc,'i',3),'flags':read(bone+0x18,'H'),
                'rotation_u16':read(bone+0x1a,'H',3),'translation_fx12':read(bone+0x20,'i',3),
                'override_file':bca(read(bone+0x2c)),'override_frame':read(bone+0x30,'H')})
        models.append({'slot':slot,'num_bones':count,'world_matrix_fx12':read(model+0x1c,'i',12),
            'num_frames_and_flags':read(model+0x54),'cursor_fx12':read(model+0x58,'i'),'speed_fx12':read(model+0x5c,'i'),
            'animation_file':bca(read(model+0x60)),'secondary_file':bca(read(model+0x64)),
            'secondary_num_frames_and_flags':read(model+0x6c),'secondary_cursor_fx12':read(model+0x70,'i'),'secondary_speed_fx12':read(model+0x74,'i'),
            'local_bones':local,'bone_matrices_fx12':[read(transforms+i*48,'i',12) for i in range(count)]})
    return {'schema':'sm64ds-pose-telemetry-v1','checkpoint':checkpoint or Path(path).stem,'phase':phase,
        'ram_sha256':hashlib.sha256(data).hexdigest(),'body_model_id':read(player+0x6db,'B'),
        'pose_controls':{'hex_742':read(player+0x742,'B'),'hex_743':read(player+0x743,'B'),'hex_75c_766':read(player+0x75c,'h',6)},'models':models}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('ram',type=Path);ap.add_argument('--checkpoint');ap.add_argument('--phase',default='ram_snapshot');args=ap.parse_args()
    print(json.dumps(inspect_ram(args.ram,args.checkpoint,args.phase),indent=2))
