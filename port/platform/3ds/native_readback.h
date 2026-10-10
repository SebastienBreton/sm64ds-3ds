#ifndef SM64DS_NATIVE_READBACK_H
#define SM64DS_NATIVE_READBACK_H
#include <array>
#include <cstdint>
// PICA stores an eight-bit approximation of (DS alpha + 1) / 32.
// Resolve the inverse once rather than dividing for every read-back pixel.
inline constexpr auto native_alpha_table=[](){
    std::array<uint32_t,256> table{};
    for(unsigned weight=0;weight<256;++weight){
        const unsigned levels=(weight*32+127)/255;
        const unsigned alpha5=levels?levels-1:0;
        table[weight]=((alpha5<<3)|(alpha5>>2))<<24;
    }
    return table;
}();
inline uint32_t decode_native_pixel(uint32_t rgba){
    return (rgba>>8)|native_alpha_table[rgba&255];
}
#endif
