// DS compiler-runtime entry points pass IEEE-754 words in integer registers.
// Keep that calling convention even though devkitARM uses hardware floats.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// DS FP status word, config/arm9 BSS020aa3f4..020aa3f8.
extern "C" { uint32_t data_020aa3f4; }
static void fp_exception(uint32_t flag)
{
    if (data_020aa3f4 & (flag << 8)) {
        fprintf(stderr, "FATAL: enabled DS FP exception 0x%x has no native dispatcher\n", flag);
        abort();
    }
    data_020aa3f4 |= flag;
}
static bool is_nan_word(uint32_t bits)
{ return (bits & 0x7fffffffU) > 0x7f800000U; }

extern "C" uint32_t func_01ffa4bc(int32_t value)
{
    const float converted = (float)value;
    uint32_t bits;
    memcpy(&bits, &converted, sizeof bits);
    return bits;
}

// 0x01ff98f4: signed floating-point less-than (movlt 1 / movge 0).
// NaNs compare false and either sign of zero compares equal.
extern "C" int func_01ff98f4(uint32_t a, uint32_t b)
{
    if (is_nan_word(a) || is_nan_word(b)) { fp_exception(1); return 0; }
    float left, right;
    memcpy(&left, &a, sizeof left);
    memcpy(&right, &b, sizeof right);
    return left < right;
}

// 0x01ff99a4..01ff99dc ends movgt1/movle0; same NaN path as less-than.
extern "C" int func_01ff99a4(uint32_t a, uint32_t b)
{
    if (is_nan_word(a) || is_nan_word(b)) { fp_exception(1); return 0; }
    float left, right;
    memcpy(&left, &a, sizeof left);
    memcpy(&right, &b, sizeof right);
    return left > right;
}

// 0x01ffa344: truncate toward zero, set inexact (0x10); invalid values
// saturate by sign and set invalid (1). -2^31 is explicitly valid in ROM.
// Integer decoding avoids undefined C float-to-int overflow and host rounding.
extern "C" int32_t func_01ffa344(uint32_t bits)
{
    const uint32_t magnitude = bits & 0x7fffffffU;
    const unsigned exponent = magnitude >> 23;
    if (exponent >= 158) {
        if (bits != 0xcf000000U) fp_exception(1);
        return bits >> 31 ? INT32_MIN : INT32_MAX;
    }
    if (exponent < 127) {
        if (magnitude) fp_exception(0x10);
        return 0;
    }
    const unsigned power = exponent - 127;
    const uint32_t significand = (magnitude & 0x7fffffU) | 0x800000U;
    uint32_t integer;
    if (power >= 23) integer = significand << (power - 23);
    else {
        const unsigned shift = 23 - power;
        integer = significand >> shift;
        if (significand & ((1U << shift) - 1)) fp_exception(0x10);
    }
    return bits >> 31 ? -(int32_t)integer : (int32_t)integer;
}

// 0x01ff9378 / 0x01ffa594: single-precision add / subtract on IEEE words in
// r0/r1 (ITCM hand-asm runtime). Round-to-nearest like the DS runtime; used
// explicitly by daKuriKing_c (bank 74).
extern "C" uint32_t func_01ff9378(uint32_t a, uint32_t b)
{
    float left, right, sum;
    memcpy(&left, &a, sizeof left);
    memcpy(&right, &b, sizeof right);
    sum = left + right;
    uint32_t bits;
    memcpy(&bits, &sum, sizeof bits);
    return bits;
}
extern "C" uint32_t func_01ffa594(uint32_t a, uint32_t b)
{
    float left, right, diff;
    memcpy(&left, &a, sizeof left);
    memcpy(&right, &b, sizeof right);
    diff = left - right;
    uint32_t bits;
    memcpy(&bits, &diff, sizeof bits);
    return bits;
}
