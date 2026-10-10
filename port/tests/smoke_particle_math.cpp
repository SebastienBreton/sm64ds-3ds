// Exercise the recovered 16-bit particle normalizer through the DS math HAL.
#include "ntr/mmio.h"
#include <cstdio>
#include <initializer_list>
extern "C" void func_02053380(short*, short*);
int main() {
    if (!ntr::io_init()) return 2;
    struct Case { short input[3], expected[3]; };
    Case cases[] = {
        {{4096,0,0},{4096,0,0}},
        {{0,-4096,0},{0,-4096,0}},
        {{0,0,1},{0,0,4096}},
        {{3000,4000,0},{2458,3277,0}},
        {{-3000,0,4000},{-2458,0,3277}},
        {{32767,-32768,12345},{2799,-2799,1054}},
        {{1,1,1},{2048,2048,2048}}
    };
    for (auto &c : cases) {
        for (unsigned ime : {0u, 1u}) {
            ntr::io_write(0x04000208, ime, 2);
            short output[3] = {};
            func_02053380(c.input, output);
            for (int i=0; i<3; ++i) {
                if (output[i] != c.expected[i]) {
                    std::printf("FAIL particle normalization axis%d got%d expected%d\n", i, output[i], c.expected[i]);
                    return 1;
                }
            }
            if (ntr::io_read(0x04000208, 2) != ime) return 1;
        }
    }
    std::printf("smoke_particle_math:7 fixed-point vectors, both interrupt states passed\n");
    return 0;
}
