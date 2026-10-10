#pragma once
struct NativeOverlayInfo { unsigned id,original_start,original_size,numeric_bytes,kind; };
enum NativeOverlayResult { OVERLAY_OK,OVERLAY_UNKNOWN,OVERLAY_UNSUPPORTED,OVERLAY_OVERLAP,OVERLAY_NO_SLOT,OVERLAY_INIT_FAILED,OVERLAY_NOT_LOADED };
extern "C" {
extern const NativeOverlayInfo sm64ds_native_overlays[];
extern const unsigned sm64ds_native_overlay_count;
NativeOverlayResult sm64ds_try_load_overlay(unsigned id);
NativeOverlayResult sm64ds_try_unload_overlay(unsigned id);
void LoadOverlay(int id);
void UnloadOverlay(int id);
}
