// Optional allocation trace around the unchanged original Model allocator.
#include <cstdio>
#include <cstdlib>
#include "BMD_File.h"
extern "C" {
extern unsigned data_020a4bc8, data_020a4be8, data_020a4be0, data_020a4bdc;
extern unsigned data_0209d3bc;
unsigned __real__ZN5Model13GetVramOffsetEj(unsigned);
void __real__ZN5Model13LoadTexAndPalER8BMD_File(BMD_File&);
void __wrap__ZN5Model13LoadTexAndPalER8BMD_File(BMD_File& file)
{
    if (std::getenv("MODEL_VRAM_TRACE")) {
        std::fprintf(stderr, "Model upload: file=%u model=%p count=%u textures=%p caller=%p\n",
                     data_0209d3bc, &file, file.numTextures, file.textures,
                     __builtin_return_address(0));
        for (unsigned i=0; i<file.numTextures && i<64; ++i) {
            const auto& t=file.textures[i];
            std::fprintf(stderr, "  texture%u data=%p size=%u flags=%08x\n",i,t.data,t.size,t.flags);
        }
        std::fflush(stderr);
    }
    __real__ZN5Model13LoadTexAndPalER8BMD_File(file);
}
unsigned __wrap__ZN5Model13GetVramOffsetEj(unsigned size)
{
    if (std::getenv("MODEL_VRAM_TRACE")) {
        std::fprintf(stderr, "Model VRAM request: file=%u bytes=%u free=%u,%u\n",
                     data_0209d3bc, size, data_020a4be8-data_020a4bc8,
                     data_020a4bdc-data_020a4be0);
        std::fflush(stderr);
    }
    return __real__ZN5Model13GetVramOffsetEj(size);
}
}
