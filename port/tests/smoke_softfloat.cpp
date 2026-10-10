// Native ARM raw-register ABI checks for the DS compiler-runtime adapters.
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
extern "C" uint32_t func_01ffa4bc(int32_t);
extern "C" int func_01ff99a4(uint32_t, uint32_t);
extern "C" int32_t func_01ffa344(uint32_t);
extern "C" uint32_t data_020aa3f4;
int main()
{
    struct Case { uint32_t bits; int32_t result; uint32_t flags; };
    const Case cases[] = {
        {0,0,0}, {0x80000000,0,0}, {0x3f800000,1,0},
        {0x3fc00000,1,0x10}, {0xbfc00000,-1,0x10},
        {1,0,0x10}, {0x80000001,0,0x10},
        {0x4effffff,2147483520,0}, {0x4f000000,INT32_MAX,1},
        {0xcf000000,INT32_MIN,0}, {0xcf000001,INT32_MIN,1},
        {0x7f800000,INT32_MAX,1}, {0xff800000,INT32_MIN,1},
        {0x7fc00000,INT32_MAX,1}, {0xffc00000,INT32_MIN,1}
    };
    for (const auto &c : cases) {
        data_020aa3f4 = 0;
        const int32_t actual = func_01ffa344(c.bits);
        if (actual != c.result || data_020aa3f4 != c.flags) {
            printf("FAIL conversion %08lx result=%ld flags=%lx\n",
                   (unsigned long)c.bits, (long)actual, (unsigned long)data_020aa3f4);
            return 1;
        }
    }
    // Same raw-word int->float->comparison chain used by the Player caller.
    if (!func_01ff99a4(func_01ffa4bc(196609), 0x48400000) ||
         func_01ff99a4(func_01ffa4bc(196608), 0x48400000) ||
         func_01ff99a4(0x80000000,0)) return 1;
    data_020aa3f4 = 0;
    if (func_01ff99a4(0x7fc00000,0) || data_020aa3f4 != 1) return 1;
    printf("smoke_softfloat:15 conversion cases and native Player comparison chain passed\n");
    return 0;
}
