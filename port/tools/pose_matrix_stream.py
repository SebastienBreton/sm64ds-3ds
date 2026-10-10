#!/usr/bin/env python3
"""Decode original CPU matrix commands from DS or native observation streams.

Native matrix payload block copies have zero I/O origin: accept them only while
a CPU matrix command explicitly opened through I/O is awaiting its payload.
Other zero-origin words (including display-list submission) are excluded.
No geometry is
changed or replayed here. Unknown FIFO opcodes and unfinished commands make the
capture explicitly incomplete, rather than yielding a false equality result.
"""
import argparse,json
from pathlib import Path

PARAMS={0x10:1,0x11:0,0x12:1,0x13:1,0x14:1,0x15:0,0x16:16,0x17:12,0x18:16,0x19:12,0x1a:9,0x1b:3,0x1c:3}


def decode(path):
    commands=[];fifo_queue=[];fifo_opcode=None;fifo_args=[];direct_opcode=None;direct_args=[];errors=[];excluded=0;block_payload=0
    def select_fifo():
        nonlocal fifo_opcode,fifo_args
        fifo_opcode=None;fifo_args=[]
        while fifo_queue:
            opcode=fifo_queue.pop(0)
            if not opcode:continue
            if opcode not in PARAMS:errors.append(f'Unknown CPU FIFO opcode {opcode:#x}');continue
            if not PARAMS[opcode]:commands.append({'opcode':opcode,'args':[]});continue
            fifo_opcode=opcode;break
    for line in Path(path).read_text().splitlines():
        row=json.loads(line)
        if 'fifo' in row:
            origin=row.get('io_origin')
            if origin is None:errors.append('Native FIFO origin missing');continue
            if origin==0 and fifo_opcode is not None:
                # runtime.cpp Copy36Bytes/Copy48BytesFixed forward matrix
                # payload words directly to gx_write_fifo, outside io_write.
                block_payload+=1
            elif not 0x04000400<=origin<0x04000440:excluded+=1;continue
            address=0x04000400;word=row['fifo']
        else:address=row['port'];word=row['word']
        if 0x04000400<=address<0x04000440:
            if fifo_opcode is None:
                fifo_queue=[(word>>(8*i))&255 for i in range(4)];select_fifo()
            else:
                fifo_args.append(word)
                if len(fifo_args)==PARAMS[fifo_opcode]:commands.append({'opcode':fifo_opcode,'args':fifo_args});select_fifo()
        elif 0x04000440<=address<=0x04000470:
            opcode=(address-0x04000400)//4
            if opcode not in PARAMS:errors.append(f'Unknown matrix port {address:#x}');continue
            if not PARAMS[opcode]:commands.append({'opcode':opcode,'args':[]});continue
            if direct_opcode is None:direct_opcode=opcode
            if direct_opcode!=opcode:errors.append('Interrupted direct matrix command');direct_opcode=opcode;direct_args=[]
            direct_args.append(word)
            if len(direct_args)==PARAMS[opcode]:commands.append({'opcode':opcode,'args':direct_args});direct_opcode=None;direct_args=[]
    if fifo_opcode is not None or direct_opcode is not None:errors.append('Unfinished matrix command')
    return {'schema':'sm64ds-pose-matrix-stream-v1','complete':not errors,'errors':errors,'excluded_unattributed_fifo_words':excluded,'cpu_block_payload_words':block_payload,'commands':commands}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('stream',type=Path);ap.add_argument('--compare',type=Path);args=ap.parse_args();result=decode(args.stream)
    if args.compare:
        other=decode(args.compare);result={'both_complete':result['complete'] and other['complete'],'matrix_commands_equal':result['complete'] and other['complete'] and result['commands']==other['commands'],'first':result,'second':other}
    print(json.dumps(result,indent=2))
