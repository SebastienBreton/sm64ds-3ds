#include "sound_native.h"
#include <cstdio>
#include <cstdlib>

extern "C" {
int func_02050a5c(unsigned);
void* func_020511d4(void*, unsigned, unsigned, unsigned, unsigned);
int func_020509d8(unsigned, void*, int, int);
void _ZN4CP1514FlushDataCacheEjj(unsigned, unsigned);

// Same allocation/read/callback lifetime as src/func_02051634.c. The native
// command processor needs the bounded file view that ARM7 read from shared RAM.
void* func_02051634(unsigned file, unsigned callback, unsigned archive,
                    unsigned id, void* owner) {
    const int size=func_02050a5c(file);
    if (!size) return nullptr;
    void* data=func_020511d4(owner,size+0x20,callback,archive,id);
    if (!data || func_020509d8(file,data,size,0)!=size) return nullptr;
    _ZN4CP1514FlushDataCacheEjj(reinterpret_cast<unsigned>(data),size);
    if (!sm64ds_native_sound_register({data,static_cast<unsigned>(size)})) {
        std::fprintf(stderr,"native sound: cannot register archive file %u (%d bytes)\n",file,size);
        std::abort();
    }
    // Heap callbacks enqueue invalidation. Keep this view registered until that
    // command executes, so earlier queued starts retain their original order.
    return data;
}
}
