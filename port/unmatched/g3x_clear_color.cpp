// Portable ABI boundary: the retail bool occupies a full DS stack argument.
// Reading an int through a native bool local would overread its object.
#include "ntr/mmio.h"
struct G3X {
    static void SetClearColor(unsigned short color, int alpha, int depth, int id, bool fog);
};
void G3X::SetClearColor(unsigned short color, int alpha, int depth, int id, bool fog) {
    const uint32_t packed=uint32_t(color)|(uint32_t(alpha)<<16)|(uint32_t(id)<<24)|(fog?0x8000u:0u);
    ntr::io_write(0x04000350,packed,4);
    ntr::io_write(0x04000354,uint16_t(depth),2);
}
