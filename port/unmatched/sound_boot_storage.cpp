// ARM9 boot-owned audio storage; extents checked against symbols.txt.
#include <cstdint>
extern "C" {
char data_0208e498[]="/data/sound_data.sdat";
alignas(4) unsigned char data_0209b4b4[0x88]; // SDAT owner, including FSFile
alignas(32) unsigned char data_0209baa0[0x1000]; // stream scratch
unsigned data_020a4bf0[2];
unsigned data_020a4c08[2][2];
unsigned data_020a4d44;
unsigned data_020a6480;
// Nine original parameter pairs used by func_02048fd4 at ARM9 0x020821c0.
unsigned data_020821c0[18]={0x5a,0x226,0xc8,0x5dc,0xc8,0x2ee,0x96,0x190,0x28,0x190,0x5a,0x3e8,0x32,0x190,0xc8,0x5dc,0x1451,0xa28};
}
