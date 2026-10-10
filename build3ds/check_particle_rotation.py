#!/usr/bin/env python3
"""Compare native rotation stores with actual DS Thumb code via Unicorn.
Requires the local ROM extraction, a compiled native dylib, and unicorn.
"""
import ctypes
from pathlib import Path
import random
import struct
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR

ROOT = Path(__file__).resolve().parent.parent
blob = (ROOT / "sm64ds-decomp/extracted/arm9_dec.bin").read_bytes()
lib = ctypes.CDLL(str(ROOT / "build3ds/validation/particle-rotation.dylib"))
emu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
emu.mem_map(0x02000000, 0x400000)
emu.mem_write(0x02004000, blob)
END = 0x02300000
DEST = 0x02301004
rng = random.Random(0x52588)
values = [0, 1, -1, 4096, -4096, 0x7fffffff, -0x80000000]
cases = [(s, c) for s in values for c in values]
cases += [(rng.randint(-0x80000000, 0x7fffffff), rng.randint(-0x80000000, 0x7fffffff)) for _ in range(1000)]
count = 0
for name, address in [("Matrix3x3_SetRotationZ", 0x02052588), ("func_0205256c", 0x0205256c)]:
    native = getattr(lib, name)
    native.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.c_int32]
    native.restype = None
    for sine, cosine in cases:
        guard = bytes([0xa5])*44
        emu.mem_write(DEST-4, guard)
        for reg, val in [(UC_ARM_REG_R0, DEST), (UC_ARM_REG_R1, sine), (UC_ARM_REG_R2, cosine), (UC_ARM_REG_LR, END|1)]:
            emu.reg_write(reg, val & 0xffffffff)
        emu.emu_start(address|1, END, count=40)
        out = (ctypes.c_int32*11).from_buffer_copy(guard)
        native(ctypes.cast(ctypes.byref(out, 4), ctypes.POINTER(ctypes.c_int32)), sine, cosine)
        actual = bytes(out)
        expected = bytes(emu.mem_read(DEST-4, 44))
        assert actual == expected, (name, sine, cosine, actual.hex(), expected.hex())
        count += 1
report = f"PASS: {count} native rotation results match original ROM Thumb execution, including guard bytes, negative values and INT_MIN.\n"
(ROOT / "build3ds/validation/particle-rotation-verification.txt").write_text(report)
print(report, end="")
