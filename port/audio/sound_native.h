#pragma once
#include "sseq_decoder.h"
// Game-thread lifecycle. Shutdown while the root heap is still alive.
bool sm64ds_native_sound_register(Sm64dsSoundBuffer);
void sm64ds_native_sound_unregister(const void*);
bool sm64ds_native_sound_render(std::int16_t*,unsigned frames);
// Realtime output is an alternative clock to offline render, never both.
bool sm64ds_native_sound_output_open();
bool sm64ds_native_sound_output_pump();
// Game thread (parked game): deferred alarms and releases of a worker pump.
bool sm64ds_native_sound_maintenance();
// Non-blocking attempt: done=false when the mixer holds the lock (then
// call the blocking maintenance and sm64ds_native_sound_retract()).
bool sm64ds_native_sound_try_maintenance(bool& done);
void sm64ds_native_sound_retract();
void sm64ds_native_sound_output_close();
bool sm64ds_native_sound_output_counts(unsigned& submitted,unsigned& completed);
const char* sm64ds_native_sound_output_error();
void sm64ds_native_sound_shutdown();
