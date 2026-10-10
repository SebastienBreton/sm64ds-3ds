#include "FaderColor.h"
#include "ntr/mmio.h"
#include <cstdio>
#include <initializer_list>
extern "C" FaderColor data_0209b294;
extern "C" FaderColor data_0209f5e8;
extern "C" unsigned char data_0209d3ac[4];
extern "C" int data_02099fd0;
static int check(bool ok, const char* what) {
    if (!ok) std::printf("FAIL: %s\n", what);
    return ok ? 0 : 1;
}
int main() {
    if (!ntr::io_init()) return 2;
    Fader* scene = &data_0209f5e8;
    if (check(scene->currInterp == 4096 && scene->speed == 0 && data_0209f5e8.color == 0, "scene color fader constructor")) return 1;
    scene->SetToStart(); scene->SetForwardTime(4);
    for (int i=1; i<=4; ++i) {
        scene->AdvanceFade();
        if (check(scene->currInterp == i*1024, "scene color fader dispatch")) return 1;
    }
    Fader* f = &data_0209b294;
    if (check(f->currInterp == 4096 && f->speed == 0 && data_0209b294.color == 0, "constructor")) return 1;
    for (unsigned color = 0; color < 2; ++color) {
        data_0209b294.color = color;
        f->SetToStart(); f->SetForwardTime(16);
        for (int i = 1; i <= 16; ++i) {
            f->AdvanceFade();
            if (check(f->currInterp == i * 256, "forward interpolation")) return 1;
            for (unsigned base : {0x04000050u, 0x04001050u}) {
                if (check(ntr::io_read(base,2) == (color ? 0xbf : 0xff) && ntr::io_read(base+4,2) == (unsigned)i, "both engines blend registers")) return 1;
            }
        }
        if (check(f->IsAtEnd() && !f->IsBetweenStartAndEnd(), "end state")) return 1;
        f->SetBackwardTime(16);
        for (int i = 15; i >= 0; --i) {
            f->AdvanceFade();
            if (check(f->currInterp == i * 256, "backward interpolation")) return 1;
        }
        if (check(f->IsAtStart() && ntr::io_read(0x04000050,2)==0 && ntr::io_read(0x04001050,2)==0, "start clears blend")) return 1;
    }
    f->SetForwardTime(0); f->AdvanceFade();
    if (check(f->IsAtEnd(), "instant forward")) return 1;
    f->SetBackwardTime(0); f->AdvanceFade();
    if (check(f->IsAtStart(), "instant backward")) return 1;
    // Check both palette copy branches used when brightness reaches black.
    for (int channel : {-1, 1}) {
        data_02099fd0 = channel;
        data_0209d3ac[0] = 0x34; data_0209d3ac[1] = 0x12;
        auto* mainPalette = static_cast<unsigned short*>(ntr::host_ptr(0x05000000));
        auto* subPalette = static_cast<unsigned short*>(ntr::host_ptr(0x05000400));
        *mainPalette = 0; *subPalette = 0;
        FaderBrightness brightness;
        Fader* b = &brightness;
        b->SetToStart(); b->SetForwardTime(16);
        for (int i = 1; i <= 16; ++i) {
            b->AdvanceFade();
            if (check(ntr::io_read(0x0400006c,2) == (unsigned)(0x8000|i) &&
                      ntr::io_read(0x0400106c,2) == (unsigned)(0x8000|i), "master brightness")) return 1;
        }
        if (check(*mainPalette == 0x1234 && *subPalette == 0x1234, "backdrop CPU/DMA copy")) return 1;
        b->SetBackwardTime(0); b->AdvanceFade();
        if (check(ntr::io_read(0x0400006c,2) == 0 && ntr::io_read(0x0400106c,2) == 0, "brightness cleared")) return 1;
    }
    data_02099fd0 = -1;
    std::printf("smoke_fader: native construction, virtual dispatch, black/white fades, both register banks and CPU/DMA palette copies passed\n");
    return 0;
}
