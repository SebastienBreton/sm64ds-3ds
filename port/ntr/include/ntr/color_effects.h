#pragma once
#include <stdint.h>

namespace ntr {
// DS alpha blend: six-bit channels, coefficients clamped to16, +8 rounding
// before /16. Two16-bit lanes let ARM11 multiply red/blue together without
// carry between channels (the largest lane numerator is2024).
struct AlphaBlend {
    unsigned eva,evb;
    AlphaBlend(unsigned a,unsigned b):eva(a<16?a:16),evb(b<16?b:16){}
    uint32_t operator()(uint32_t front,uint32_t back) const {
        uint32_t rb=((((front>>2)&0x003f003fu)*eva+
                      ((back>>2)&0x003f003fu)*evb+0x00080008u)>>4)&0x007f007fu;
        // Bit6 marks lanes above63. Saturate those lanes independently.
        rb=(rb|(((rb&0x00400040u)>>6)*63u))&0x003f003fu;
        unsigned green=((((front>>10)&63)*eva+((back>>10)&63)*evb+8)>>4);
        if(green>63)green=63;
        return 0xff000000u|(rb<<2)|((rb>>4)&0x00030003u)|
               (((green<<2)|(green>>4))<<8);
    }
};
}
