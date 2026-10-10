#pragma once
#include <cstdlib>

// Install the validated rendering path before any renderer reads its options.
// Explicit settings, including zero, remain available for comparisons.
inline bool sm64ds_configure_renderer_defaults() {
    struct Option { const char* name; const char* value; };
    static constexpr Option defaults[] = {
        {"PLAYER_NATIVE_MESH", "1"},
        {"PLAYER_BMD_COMPILED_LISTS", "1"},
        {"PLAYER_MIXED_DEFER_INITIAL_PALETTE", "1"},
        {"PLAYER_MIXED_STATIC_CLIP_PLAN", "1"},
        {"PLAYER_MIXED_WHOLE_RUN_INSIDE", "1"},
        {"PLAYER_MIXED_UNIFIED_SHADER", "1"},
        {"PLAYER_RETAIN_HELD_RASTER", "1"},
        {"PLAYER_GPU_COMMAND_DRAIN", "1"},
        {"PLAYER_GPU_COMMAND_BUFFER_KIB", "1024"}, // ingame-stereo: room for both eyes (512 drained mid-frame in stereo)
        {"PLAYER_CLIP_SPARSE_PRODUCT", "1"},
        {"PLAYER_AUDIO_SEGMENT_VOICES", "1"},
        // DS geometry evaluation, lighting and clipping on the PICA in stages
        // (gpu-geometry-ingame validation; 7 = bit-exact subset, 0 = off).
        {"SM64DS_GX_GPU_INGAME", "15"},
    };
    for (const auto& option : defaults) {
        if (!std::getenv(option.name) && setenv(option.name, option.value, 0) != 0)
            return false;
    }
    return true;
}
