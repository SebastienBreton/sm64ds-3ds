// Native diagnostic display replacing DS BG/VRAM-only crash-screen rendering.
// Reporting remains nonfatal, as in ShowCrashScreen; game Crash() still aborts.
#include <cstdio>
#include <cstdint>
extern "C" {
extern int data_02099f24;
extern unsigned data_0209d3bc;
extern unsigned short data_020a4b50;
extern unsigned char data_020a4b4c;
extern void *data_020a4b68;
extern unsigned data_020a4bc8, data_020a4be8, data_020a4be0, data_020a4bdc, data_020a4be4;
extern unsigned data_020a4bcc, data_020a4bd8;
extern unsigned short data_020a6088[];
unsigned char data_0209cdd0[4]; // display flag, next ARM9 BSS symbol at+4
}
static bool reported;
static int last_phase;
static unsigned last_file, last_actor, last_kind;
static void *last_iterator;
extern "C" void ShowCrashScreen()
{
    if (!reported || last_phase != data_02099f24 || last_file != data_0209d3bc ||
        last_actor != data_020a4b50 || last_kind != data_020a4b4c ||
        last_iterator != data_020a4b68) {
        fprintf(stderr, "Game diagnostic: phase=%d file=%u pending_actor=0x%04x kind=%u iterator=%p\n",
                data_02099f24, data_0209d3bc, data_020a4b50,
                data_020a4b4c, data_020a4b68);
        fprintf(stderr, "VRAM diagnostic: texmask=%04x plttmask=%04x arenas=%x..%x,%x..%x uploaded=%x palette=%x..%x\n",
                data_020a6088[4], data_020a6088[5], data_020a4bc8, data_020a4be8,
                data_020a4be0, data_020a4bdc, data_020a4be4, data_020a4bcc, data_020a4bd8);
        fflush(stderr);
        reported = true;
        last_phase = data_02099f24; last_file = data_0209d3bc;
        last_actor = data_020a4b50; last_kind = data_020a4b4c;
        last_iterator = data_020a4b68;
    }
    data_0209cdd0[0] = 1;
}
extern "C" void InitCrashScreen()
{
    reported = false;
    fprintf(stderr, "Native game diagnostic display (full DS register/heap dump not yet ported)\n");
    ShowCrashScreen();
}
