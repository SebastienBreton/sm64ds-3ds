// include/daDonketu_c.h still declares CleanupResources/Render overrides, but
// the retail vtable (ov064 0x0211b870 slots 3/9) holds daOts_c's 0x02116ca0 /
// 0x02116cf0: daDonketu_c inherits them. Forward to the base bodies so GCC's
// vtable carries exactly the retail behaviour.
#include "daDonketu_c.h"
int daDonketu_c::CleanupResources() { return daOts_c::CleanupResources(); }
int daDonketu_c::Render() { return daOts_c::Render(); }
