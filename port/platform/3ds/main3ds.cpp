// 3DS entry for a port smoke runner. The runner is compiled with
// -Dmain=smoke_main, which keeps C++ linkage, so both signatures the smokes use
// are declared weak and whichever one the runner defined is called.
//
// Normal mode: console on the top screen, wait for START.
// Auto mode (sdmc:/sm64ds/autoexit exists): stdout/stderr go to
// sdmc:/sm64ds/log.txt and the app exits, so a host script can iterate.
#undef main
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cpu_mode.h"

int smoke_main() __attribute__((weak));
int smoke_main(int argc, char **argv) __attribute__((weak));

// libctru's default main-thread stack is 32 KB; the smokes (and the game's
// frame functions) were written against a 1 MB Windows default.
extern "C" { u32 __stacksize__ = 1024 * 1024; }

extern "C" int sm64ds_backlight_init() __attribute__((weak));
extern char **environ;
static char *g_empty_env[] = {nullptr};

int main(int argc, char **argv)
{
    // The 3dsx loader leaves no usable process environment (getenv returned
    // garbage), so start from an empty one and point the asset catalog at the
    // SD card copy of build/assets + extracted/.
    environ = g_empty_env;
    setenv("SM64DS_ASSET_ROOT", "sdmc:/sm64ds/repo", 1);
    // Knobs the Windows smokes take from the environment: one KEY=VALUE per
    // line in sdmc:/sm64ds/env.txt.
    if (FILE *ef = fopen("sdmc:/sm64ds/env.txt", "r")) {
        char line[256];
        while (fgets(line, sizeof line, ef)) {
            line[strcspn(line, "\r\n")] = 0;
            if (char *eq = strchr(line, '=')) {
                *eq = 0;
                setenv(line, eq + 1, 1);
            }
        }
        fclose(ef);
    }
    // Smokes write their BMPs/frames relative to the working directory.
    chdir("sdmc:/sm64ds");
    gfxInitDefault();
    const bool autoexit = access("sdmc:/sm64ds/autoexit", F_OK) == 0;
    if (autoexit) {
        // One stream for both: the sdmc devoptab ignores O_APPEND, so two
        // FILEs on the same path overwrite each other's lines. newlib's
        // stderr is an lvalue (_REENT->_stderr), so alias it to stdout.
        freopen("sdmc:/sm64ds/log.txt", "w", stdout);
        stderr = stdout;
        setvbuf(stdout, NULL, _IONBF, 0);
    } else {
        consoleInit(GFX_TOP, NULL);
    }
    printf("sm64ds port smoke on 3DS\n\n");
    sm64ds_configure_cpu_mode();
    if (sm64ds_backlight_init) {
        const int power_result=sm64ds_backlight_init();
        if (power_result) {
            printf("FATAL: native backlight initialization failed: %08x\n",power_result);
            return 1;
        }
    }
    int (*no_args)() = smoke_main;
    int (*with_args)(int, char **) = smoke_main;
    const int rc = no_args ? no_args() : with_args ? with_args(argc, argv) : 99;
    printf("\nsmoke exit code: %d\n%s\n", rc, rc == 0 ? "PASS" : "FAIL");
    if (!autoexit) {
        printf("\nPress START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
    }
    fflush(stdout);
    gfxExit();
    return rc;
}
