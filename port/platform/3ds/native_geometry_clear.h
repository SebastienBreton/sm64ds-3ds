#pragma once
#include <cstdint>
#include <cstring>
namespace ds_clear {
// DS renderer state sampled at presentation. RGB is the geometry-engine
// expansion, distinct from the even six-bit channels of a 2D palette.
struct State {
    uint32_t color=0x3f000000,depth=0x7fff;
    uint16_t display=0;
    // Fog registers (FOG_COLOR, FOG_OFFSET, FOG_TABLE), sampled with the clear.
    uint32_t fog_color=0;
    uint16_t fog_offset=0;
    uint8_t fog_table[32]={};
    bool fog_enabled()const{return (display&(1u<<7))!=0;}
    bool fog_alpha_only()const{return (display&(1u<<6))!=0;}
    unsigned fog_shift()const{return (display>>8)&15;}
    // Reads every input of one presented 3D frame through the IO mirror.
    template<class Read> static State capture(Read read){
        State s;
        s.color=uint32_t(read(0x04000350,4));s.depth=uint32_t(read(0x04000354,4));s.display=uint16_t(read(0x04000060,2));
        s.fog_color=uint32_t(read(0x04000358,4));s.fog_offset=uint16_t(read(0x0400035c,2)&0x7fff);
        for(unsigned i=0;i<32;++i)s.fog_table[i]=uint8_t(read(0x04000360+i,1)&0x7f);
        return s;
    }
    unsigned alpha()const{return (color>>16)&31;}
    unsigned polygon_id()const{return (color>>24)&63;}
    bool image()const{return (display&(1u<<14))!=0;}
    bool fog()const{return (display&(1u<<7)) && (color&(1u<<15));}
    bool supported()const{return !image() && !fog();}
    static unsigned channel(unsigned five){
        const unsigned six=five?five*2+1:0;return (six<<2)|(six>>4);
    }
    uint32_t argb()const{
        const unsigned a=alpha();
        return ((a<<3)|(a>>2))<<24 | channel(color&31)<<16 |
            channel((color>>5)&31)<<8 | channel((color>>10)&31);
    }
    // PICA stores the blend weight (alpha5+1)/32; readback restores alpha5.
    uint32_t rgba()const{
        const auto c=argb();const unsigned a=alpha();
        const unsigned weight=a==0?0:a==31?255:((a+1)*255+16)/32;
        return (c<<8)|weight;
    }
    uint32_t reversed_depth()const{return 0xffffffu-(((depth&0x7fff)*0x200u)+0x1ffu);}
    bool operator==(const State& other)const{return color==other.color && depth==other.depth && display==other.display &&
        fog_color==other.fog_color && fog_offset==other.fog_offset && !std::memcmp(fog_table,other.fog_table,sizeof fog_table);}
};
}
