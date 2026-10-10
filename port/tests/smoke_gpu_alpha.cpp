// Isolate the GPU raster oracle from native scene/bootstrap state.
#include "ntr/mmio.h"
#include "ntr/gx.h"
#include "Heap.h"
#include "platform/3ds/viewer_3ds.h"
#include <cstdlib>
#include <cstdio>
bool player_present(bool capture,bool perspective=false);
int main() {
    if(!ntr::io_init() || !Heap::SetupRootHeap())return 1;
    setenv("PLAYER_ALPHA_PROBE","1",1);
    ntr::gx_reset();
    v3ds_renderer_init();
    for(int frame=0;frame<3;++frame)if(!player_present(frame==2))return 2;
    puts("GPU alpha capture complete; validate with check_capture.py");
    return 0;
}
