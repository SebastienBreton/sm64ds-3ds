#!/usr/bin/env python3
"""Check raw DS float words across signed/NaN/rounding boundaries."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = r"""
#include <cstdint>
#include <cassert>
extern "C" uint32_t func_01ffa4bc(int32_t);
extern "C" int func_01ff98f4(uint32_t, uint32_t);
extern "C" int func_01ff99a4(uint32_t, uint32_t);
extern "C" int32_t func_01ffa344(uint32_t);
extern "C" uint32_t data_020aa3f4;
void conversion(uint32_t word, int32_t expected, uint32_t flags) {
    data_020aa3f4 = 0;
    assert(func_01ffa344(word) == expected);
    assert(data_020aa3f4 == flags);
}
int main() {
    assert(func_01ffa4bc(0) == 0);
    assert(func_01ffa4bc(1) == 0x3f800000);
    assert(func_01ffa4bc(-1) == 0xbf800000);
    assert(func_01ffa4bc(INT32_MIN) == 0xcf000000);
    assert(func_01ffa4bc(INT32_MAX) == 0x4f000000);
    assert(func_01ffa4bc(16777217) == 0x4b800000);
    assert(func_01ff98f4(0xbf800000, 0) == 1);
    assert(func_01ff98f4(0x80000000, 0) == 0);
    assert(func_01ff98f4(0, 0x80000000) == 0);
    assert(func_01ff98f4(0xff800000, 0x7f800000) == 1);
    assert(func_01ff98f4(0x7fc00000, 0) == 0);
    assert(func_01ff98f4(0, 0x7fc00000) == 0);
    assert(data_020aa3f4 == 1);
    data_020aa3f4 = 0;
    assert(func_01ff99a4(0x3f800000, 0) == 1);
    assert(func_01ff99a4(0xbf800000, 0) == 0);
    assert(func_01ff99a4(0, 0x80000000) == 0);
    assert(func_01ff99a4(0x7f800000, 0x3f800000) == 1);
    assert(func_01ff99a4(0x7fc00000, 0) == 0);
    assert(data_020aa3f4 == 1);
    conversion(0, 0, 0);
    conversion(0x80000000, 0, 0);
    conversion(0x3f800000, 1, 0);
    conversion(0xbf800000, -1, 0);
    conversion(0x3fc00000, 1, 0x10);
    conversion(0xbfc00000, -1, 0x10);
    conversion(0x3f000000, 0, 0x10);
    conversion(0xbf000000, 0, 0x10);
    conversion(1, 0, 0x10);
    conversion(0x80000001, 0, 0x10);
    conversion(0x4effffff, 2147483520, 0);
    conversion(0x4f000000, INT32_MAX, 1);
    conversion(0xcf000000, INT32_MIN, 0);
    conversion(0xcf000001, INT32_MIN, 1);
    conversion(0x7f800000, INT32_MAX, 1);
    conversion(0xff800000, INT32_MIN, 1);
    conversion(0x7fc00000, INT32_MAX, 1);
    conversion(0xffc00000, INT32_MIN, 1);
    data_020aa3f4 = 2;
    assert(func_01ffa344(0x3fc00000) == 1);
    assert(data_020aa3f4 == 0x12);
}
"""
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / "test.cpp").write_text(source)
    subprocess.run(["c++", "-std=c++11", str(p / "test.cpp"),
                    str(root / "hal/softfloat_abi.cpp"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
print("softfloat ABI: conversion/comparison boundaries and sticky flags passed")
