/*
 * SSEQ Player - Player structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-10-18
 *
 * Adapted from source code of FeOS Sound System
 * By fincs
 * https://github.com/fincs/FSS
 */

#pragma once

#include <memory>
#include <array>
#include <bitset>
#include "SSEQ.h"
#include "Track.h"
#include "Channel.h"
#include "consts.h"
#include "../../audio/raw_pcm.h"
#include "../../audio/raw_capture.h"
#include "../../audio/sound_surround.h"

struct Player;
// One hardware-sized pool, shared by every sequencer in a sound service.
// Owners must outlive their release tails and this pool must outlive its players.
struct SoundPool
{
 std::array<Track, FSS_MAXTRACKS> tracks;
 std::array<Channel, 16> channels;
 std::array<int16_t, 16> globalVariables;
 unsigned lockedChannels=0,weakLockedChannels=0;
 Sm64dsRawPcm rawPcm[16];
 Sm64dsRawCapture captures[2];
 unsigned leftOutput=0,rightOutput=0;
 bool excludeChannel1=false,excludeChannel3=false;
 unsigned surroundAmount=0;
 std::uint64_t rawClockRemainder=0;
 double secondsIntoPlayback = 0, secondsUntilNextClock;
 SoundPool();
 SoundPool(const SoundPool&) = delete;
 SoundPool& operator=(const SoundPool&) = delete;
 void GenerateSamplesLegacy(std::vector<uint8_t>& buf, unsigned offset, unsigned samples,
                      unsigned rate, Player* const* players, unsigned count,
                      unsigned mute = 0);
 void GenerateSamples(std::vector<uint8_t>& buf, unsigned offset, unsigned samples,
                      unsigned rate, Player* const* players, unsigned count,
                      unsigned mute = 0);
};

struct Player
{
private:
 std::unique_ptr<SoundPool> ownedPool;
public:
 SoundPool& pool;
	uint8_t prio, nTracks;
 bool paused=false;
 uint32_t localTickCount=0;
 uint32_t* tickCount=&localTickCount;
	uint16_t tempo, tempoCount, tempoRate /* 8.8 fixed point */;
	int16_t masterVol, sseqVol;

	const SSEQ *sseq;

	uint8_t trackIds[FSS_TRACKCOUNT];
	std::array<Track, FSS_MAXTRACKS>& tracks;
	std::array<Channel, 16>& channels;
	int16_t localVariables[16];
 // A mixer binds this to persistent per-slot storage before Setup.
 int16_t* variables=localVariables;
 int16_t& Variable(unsigned index);

	uint32_t sampleRate;
	Interpolation interpolation;

	explicit Player(SoundPool* shared = nullptr);
 ~Player();
 Player(const Player&) = delete;
 Player& operator=(const Player&) = delete;

	bool Setup(const SSEQ *sseq, unsigned entryOffset = 0);
	void ClearState();
	void FreeTracks();
	void Stop(bool bKillSound);
 bool IsPlaying() const;
 void SetPaused(bool value);
 void SetExternalVolume(int16_t attenuation);
	int ChannelAlloc(int type, int prio, unsigned channelMask=0xffff,bool explicitMask=false);
	int TrackAlloc();
	void Run();
	void UpdateTracks();
	void Timer();

	/* Playback helper */
	std::bitset<16> mutes;
	void GenerateSamples(std::vector<uint8_t> &buf, unsigned offset, unsigned samples);
};
