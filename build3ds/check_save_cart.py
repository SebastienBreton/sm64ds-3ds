#!/usr/bin/env python3
"""Exercise native cart persistence in an isolated temporary directory."""
import ctypes
import os
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parent.parent
rom=(ROOT/'sm64ds-decomp/extracted/arm9_dec.bin').read_bytes()
magic=rom[0x0208ee50-0x02004000:0x0208ee58-0x02004000]
assert magic==b'ds mario'
assert (ROOT/'build3ds/reference-ds/reference.sav').stat().st_size==8192
lib=ctypes.CDLL(str(ROOT/'build3ds/validation/save-cart.dylib'))
for name in ('save_block','read_block'):
    f=getattr(lib,name);f.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_uint];f.restype=ctypes.c_int
lib.test_mode.argtypes=[ctypes.c_int]
with tempfile.TemporaryDirectory(prefix='sm64ds-save-') as directory:
    path=Path(directory)/'native.sav'
    os.environ['SM64DS_SAVE_PATH']=str(path)
    out=ctypes.create_string_buffer(4096)
    assert lib.read_block(out,68,0)==2 and not path.exists()
    for slot,size in [(0,68),(1,68),(2,68),(3,740)]:
        payload=bytes((i*37+slot)%256 for i in range(size))
        buf=ctypes.create_string_buffer(payload)
        assert lib.save_block(buf,size,slot)==0
        image=path.read_bytes();assert len(image)==8192
        value=sum(magic)
        for byte in payload:
            value=((value*2)%65536+(value//32768))^byte
        expected=value.to_bytes(2,'little')+magic+payload
        assert image[slot*128:slot*128+10+size]==expected
        assert image[4096+slot*128:4096+slot*128+10+size]==expected
        assert lib.read_block(out,size,slot)==0 and out.raw[:size]==payload
    original=path.read_bytes()
    damaged=bytearray(original);damaged[10]^=1;path.write_bytes(damaged)
    assert lib.read_block(out,68,0)==0
    assert out.raw[:68]==original[10:78]
    damaged[4106]^=1;path.write_bytes(damaged)
    assert lib.read_block(out,68,0)==1
    damaged[2:10]=bytes(8);damaged[4098:4106]=bytes(8);path.write_bytes(damaged)
    assert lib.read_block(out,68,0)==2
    path.write_bytes(original)
    for size,slot in [(4096,0),(1,32),(1,0x2000000),(1,0xffffffff)]:
        assert lib.save_block(out,size,slot)==1
    lib.test_mode(2)
    assert lib.save_block(out,68,0)==1 and lib.read_block(out,68,0)==1
    lib.test_mode(0)
    assert path.read_bytes()==original
    # A directory at the temporary path forces write failure, preserving data.
    temporary=Path(str(path)+'.tmp');temporary.mkdir()
    assert lib.save_block(out,68,0)==1 and path.read_bytes()==original
    if temporary.exists():temporary.rmdir()
    path.write_bytes(b'bad-size')
    assert lib.save_block(out,68,0)==1 and lib.read_block(out,68,0)==1
    assert path.read_bytes()==b'bad-size'
report=('PASS: native file persistence, exact ROM tag/rolling checksum, primary/mirror bytes, '
        'three68-byte files and740-byte minigame data, mirror recovery, corruption/blank '
        'classification, range overflow, wireless mode rejection, failed-write preservation '
        'and malformed-file rejection. Uses temporary files only. SD/Azahar runtime and '
        'crash durability are not verified.\n')
(ROOT/'build3ds/validation/save-cart-verification.txt').write_text(report)
print(report,end='')
