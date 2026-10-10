#pragma once
#include <cstdint>
struct Sm64dsSequence;
struct Sm64dsMixer;
struct Sm64dsSoundBuffer { const void* data; unsigned size; };
// Keep one state per cached instrument-bank file, for that file's lifetime.
struct Sm64dsBankState {
    std::uint32_t fileId=0xffffffffU;
    std::uint16_t waveArchives[4]={0xffff,0xffff,0xffff,0xffff};
};
// On 3DS all calls must run synchronously on the game thread after root-heap
// setup. The root must outlive every decoder; do not call from the NDSP worker.
extern "C" {
Sm64dsSequence* sm64ds_sseq_open(const char* path,unsigned sequence,unsigned rate);
Sm64dsSequence* sm64ds_ssar_open(const char* path,unsigned archive,unsigned sequence,unsigned rate,unsigned bank=0xffffffffU,Sm64dsBankState* state=nullptr);
// Snapshot game-loaded event data, SBNK and already-bound SWAR buffers.
Sm64dsSequence* sm64ds_sseq_open_data(Sm64dsSoundBuffer events,unsigned entryOffset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves,unsigned rate,unsigned volume=127);
bool sm64ds_sseq_render(Sm64dsSequence*,std::int16_t* stereo,unsigned frames);
// Sequencing and audible channel lifetime are distinct (release tails may remain).
bool sm64ds_sseq_playing(const Sm64dsSequence*);
unsigned sm64ds_sseq_channels(const Sm64dsSequence*);
void sm64ds_sseq_stop(Sm64dsSequence*,bool killSound);
void sm64ds_sseq_close(Sm64dsSequence*);
// One mixer owns all 16 logical players and retains retired resources until
// their channels finish. All render calls advance the whole hardware pool once.
Sm64dsMixer* sm64ds_mixer_open(unsigned rate);
void sm64ds_mixer_close(Sm64dsMixer*);
bool sm64ds_mixer_start(Sm64dsMixer*,unsigned player,Sm64dsSoundBuffer events,unsigned entryOffset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves,unsigned volume=127);
// Game thread, outside the sound service lock: decode the wave archives a
// queued start will use (pinned until sm64ds_mixer_release_prepared), so the
// locked start no longer stalls the realtime renderer. Errors are ignored here
// and reported by the start itself.
void sm64ds_mixer_prepare(Sm64dsMixer*,Sm64dsSoundBuffer events,unsigned entryOffset,Sm64dsSoundBuffer bank,const Sm64dsSoundBuffer* waves);
void sm64ds_mixer_release_prepared(Sm64dsMixer*);
// Bulk copy of every player's local variables/ticks and the globals (same
// values as sm64ds_mixer_get_variable/get_ticks, null mixer included).
void sm64ds_mixer_copy_status(const Sm64dsMixer*,std::int16_t locals[16][16],std::uint32_t ticks[16],std::int16_t globals[16]);
bool sm64ds_mixer_stop(Sm64dsMixer*,unsigned player);
bool sm64ds_mixer_pause(Sm64dsMixer*,unsigned player,bool paused);
bool sm64ds_mixer_parameter(Sm64dsMixer*,unsigned player,unsigned offset,unsigned value,unsigned width);
bool sm64ds_mixer_render(Sm64dsMixer*,std::int16_t* stereo,unsigned frames);
bool sm64ds_mixer_track_parameter(Sm64dsMixer*,unsigned player,unsigned trackMask,unsigned offset,unsigned value,unsigned width);
bool sm64ds_mixer_track_mute(Sm64dsMixer*,unsigned player,unsigned trackMask,unsigned mode);
bool sm64ds_mixer_track_channels(Sm64dsMixer*,unsigned player,unsigned trackMask,unsigned channelMask);
// Indices0..15 select persistent player-local variables;16..31 select globals.
bool sm64ds_mixer_set_variable(Sm64dsMixer*,unsigned player,unsigned index,std::int16_t value);
bool sm64ds_mixer_get_variable(const Sm64dsMixer*,unsigned player,unsigned index,std::int16_t* value);
bool sm64ds_mixer_get_ticks(const Sm64dsMixer*,unsigned player,std::uint32_t* value);
unsigned sm64ds_mixer_active(const Sm64dsMixer*);
void sm64ds_mixer_collect(Sm64dsMixer*);
unsigned sm64ds_mixer_enabled_channels(const Sm64dsMixer*);
unsigned sm64ds_mixer_channels(const Sm64dsMixer*);
unsigned sm64ds_mixer_retired(const Sm64dsMixer*);
const char* sm64ds_sseq_error();
}

extern "C" bool sm64ds_mixer_invalidate(Sm64dsMixer*,unsigned,std::uintptr_t first,std::uintptr_t last);
extern "C" void sm64ds_mixer_forget_resource(Sm64dsMixer*,const void*);
extern "C" bool sm64ds_mixer_lock_channels(Sm64dsMixer*,unsigned mask,unsigned flags,bool lock);
extern "C" bool sm64ds_mixer_pcm_command(Sm64dsMixer*,unsigned command,const std::uintptr_t args[4]);
extern "C" unsigned sm64ds_mixer_captures(const Sm64dsMixer*);
using Sm64dsAlarmCallback=void (*)(unsigned index,unsigned tag,void* context);
extern "C" void sm64ds_mixer_alarm_callback(Sm64dsMixer*,Sm64dsAlarmCallback,void*);
