#ifndef SM64DS_NATIVE_BLIT_H
#define SM64DS_NATIVE_BLIT_H
#include <array>
#include <cstdint>

// Each PICA tile is contiguous. Convert into that order instead of scattering
// a scanline across 32 tiles. Source rows are reversed because texture t=0 is
// the bottom of the DS image. The unused 64 texture rows are left untouched.
inline constexpr auto native_blit_offsets=[](){
    std::array<int,64> offsets{};
    for(unsigned i=0;i<64;++i){
        const unsigned x=(i&1)|((i>>1)&2)|((i>>2)&4);
        const unsigned y=((i>>1)&1)|((i>>2)&2)|((i>>3)&4);
        offsets[i]=int(x)-int(y)*256;
    }
    return offsets;
}();

inline void upload_native_image(uint32_t* tiled,const uint32_t* image){
    auto* destination=tiled+8*32*64;
    for(unsigned row=0;row<24;++row){
        const auto* source=image+(191-row*8)*256;
        for(unsigned column=0;column<32;++column){
            for(unsigned pixel=0;pixel<64;++pixel){
                const uint32_t color=source[native_blit_offsets[pixel]];
                destination[pixel]=(color<<8)|(color>>24);
            }
            source+=8;destination+=64;
        }
    }
}
#endif
