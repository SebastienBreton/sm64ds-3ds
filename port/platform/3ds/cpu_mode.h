#pragma once
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Request the normal enhanced CPU mode on New 3DS. Game timing remains
// governed by the DS VBlank runtime, independently of host CPU throughput.
inline void sm64ds_configure_cpu_mode()
{
    bool is_new = false;
    const Result result = APT_CheckNew3DS(&is_new);
    const char* setting = std::getenv("SM64DS_NEW3DS_SPEEDUP");
    const bool enabled = !setting || std::strcmp(setting, "0") != 0;
    if (R_SUCCEEDED(result) && is_new) osSetSpeedupEnable(enabled);
    // libctru's setter returns void: this records the request, not proof of
    // an emulator clock change. The override supports comparable A/B runs.
    std::printf("CPU mode: model_query=%08lx new3ds=%d speedup_requested=%d\n",
                static_cast<unsigned long>(result), is_new,
                R_SUCCEEDED(result) && is_new && enabled);
    if (const char* required = std::getenv("SM64DS_REQUIRE_3DS_MODEL")) {
        const bool old_requested = std::strcmp(required, "old") == 0;
        const bool new_requested = std::strcmp(required, "new") == 0;
        if ((!old_requested && !new_requested) || R_FAILED(result) || is_new != new_requested) {
            std::fprintf(stderr, "FATAL: 3DS model requirement %s did not match APT query\n", required);
            std::abort();
        }
    }
}
