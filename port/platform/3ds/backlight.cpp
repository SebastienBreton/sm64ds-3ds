// DS power command adapter for the native3DS display owner.
// DS selector0 is top (PM bit3),1 bottom (bit2),2 both; see libnds system.h.
#include <3ds.h>
namespace {
int top=-1,bottom=-1;
}
extern "C" int func_0205f9ac(int screen,int enabled) {
    if (screen<0 || screen>2 || (enabled!=0 && enabled!=1)) return 0xffff;
    Result result=gspLcdInit();
    if (R_FAILED(result)) return result;
    const u32 mask=screen==0?GSPLCD_SCREEN_TOP:
                   screen==1?GSPLCD_SCREEN_BOTTOM:GSPLCD_SCREEN_BOTH;
    result=enabled?GSPLCD_PowerOnBacklight(mask):GSPLCD_PowerOffBacklight(mask);
    gspLcdExit();
    if (R_FAILED(result)) return result;
    if (screen!=1) top=enabled;
    if (screen!=0) bottom=enabled;
    return 0;
}
extern "C" int func_0205f8e0(int* a,int* b) {
    // Logical app-owned state, established by successful service commands.
    // This is not a hardware query. APT sleep/resume reconciliation belongs
    // to the native app lifecycle, not a guessed PM register value.
    if (top<0 || bottom<0) return 0xffff;
    if (a) *a=top;
    if (b) *b=bottom;
    return 0;
}
extern "C" int sm64ds_backlight_init() { return func_0205f9ac(2,1); }
