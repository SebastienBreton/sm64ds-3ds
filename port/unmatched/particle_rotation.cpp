// Native equivalents of the SDK's Thumb matrix-store primitives. Arguments
// already contain fixed-point sine/cosine; no floating-point conversion occurs.
#include <cstdint>

extern "C" void Matrix3x3_SetRotationZ(int32_t* m, int32_t s, int32_t c) {
    m[0] = c; m[1] = s; m[2] = 0;
    m[3] = static_cast<int32_t>(0u - static_cast<uint32_t>(s));
    m[4] = c; m[5] = 0;
    m[6] = 0; m[7] = 0; m[8] = 4096;
}

extern "C" void func_0205256c(int32_t* m, int32_t s, int32_t c) {
    m[0] = c; m[1] = 0;
    m[2] = static_cast<int32_t>(0u - static_cast<uint32_t>(s));
    m[3] = 0; m[4] = 4096; m[5] = 0;
    m[6] = s; m[7] = 0; m[8] = c;
}
