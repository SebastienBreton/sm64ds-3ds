/*
 * SSEQ Player - Channel structures
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-09-17
 *
 * Adapted from source code of FeOS Sound System
 * By fincs
 * https://github.com/fincs/FSS
 *
 * Some code/concepts from DeSmuME
 * http://desmume.org/
 */

#pragma once

#include <bitset>
#include <tuple>
#include <cstdint>
#include "SWAV.h"
#include "Track.h"
#include <cstdio>
// minigames-30fps-min: PCM notes/channels found without wave samples (would
// read address 0). Logged once; such a channel is silenced and killed.
extern "C" unsigned sm64ds_sseq_null_sources;
inline void sm64ds_sseq_null_source_seen(){if(!sm64ds_sseq_null_sources++)std::printf("SSEQ channel without wave samples dropped (first occurrence)\n");}

/*
 * This structure is meant to be similar to what is stored in the actual
 * Nintendo DS's sound registers.  Items that were not being used by this
 * player have been removed, and items which help the simulated registers
 * have been added.
 */
struct NDSSoundRegister
{
	// Control Register
	uint8_t volumeMul;
	uint8_t volumeDiv;
	uint8_t panning;
	uint8_t waveDuty;
	uint8_t repeatMode;
	uint8_t format;
	bool enable;

	// Data Source Register
	const SWAV *source;

	// Timer Register
	uint16_t timer;

	// PSG Handling, not a DS register
	uint16_t psgX;
	int16_t psgLast;
	uint32_t psgLastCount;

	// DS channel timer model (GBATEK SOUNDxTMR, melonDS SPUChannel::Run): the
	// 16-bit counter starts at the reload value on key-on, advances by the
	// elapsed 16.76 MHz ticks and steps one source sample per overflow.
	// samplePosition is the source index (negative during the start delay).
	int32_t samplePosition;
	uint32_t timerCounter;

	// Loopstart Register
	uint32_t loopStart;

	// Length Register
	uint32_t length;

	uint32_t totalLength;

	NDSSoundRegister();

	void ClearControlRegister();
	void SetControlRegister(uint32_t reg);
};

/*
 * From FeOS Sound System, this is temporary storage of what will go into
 * the Nintendo DS sound registers.  It is kept separate as the original code
 * from FeOS Sound System utilized this to hold data prior to passing it into
 * the DS's registers.
 */
struct TempSndReg
{
	uint32_t CR;
	const SWAV *SOURCE;
	uint16_t TIMER;
	uint32_t REPEAT_POINT, LENGTH;

	TempSndReg();
};

struct Player;

struct Channel
{
	int8_t chnId;

	TempSndReg tempReg;
	uint8_t state;
	int8_t trackId; // -1 = none
	uint8_t prio;
	bool manualSweep;

	std::bitset<CF_BITS> flags;
	int8_t pan; // -64 .. 63
	int16_t extAmpl;

	int16_t velocity;
	int8_t extPan;
	uint8_t key;

	int ampl; // 7 fractionary bits
	int extTune; // in 64ths of a semitone

	uint8_t orgKey;

	uint8_t modType, modSpeed, modDepth, modRange;
	uint16_t modDelay, modDelayCnt, modCounter;

	uint32_t sweepLen, sweepCnt;
	int16_t sweepPitch;

	uint8_t attackLvl, sustainLvl;
	uint16_t decayRate, releaseRate;

	/*
	 * These were originally global variables in FeOS Sound System, but
	 * since they were linked to a certain channel anyways, I moved them
	 * into this class.
	 */
	int noteLength;
	uint16_t vol;

	const Player *ply;
	NDSSoundRegister reg;

	/*
	 * Interpolation history buffer, which contains the maximum number of
	 * samples required for any given interpolation mode. Doubled to
	 * simplify the case of wrapping. Thanks to kode54 for providing this.
	 */
	uint32_t sampleHistoryPtr;
	int16_t sampleHistory[64];

	/*
	 * Lookup tables for the Sinc interpolation, to
	 * avoid the need to call the sin/cos functions all the time.
	 * These are static as they will not change between channels or runs
	 * of the program.
	 */
	static bool initializedLUTs;
	static const unsigned SINC_RESOLUTION = 8192;
	static const unsigned SINC_WIDTH = 8;
	static const unsigned SINC_SAMPLES = SINC_RESOLUTION * SINC_WIDTH;
	static double sinc_lut[SINC_SAMPLES + 1];
	static double window_lut[SINC_SAMPLES + 1];

	Channel();

	void UpdateVol(const Track &trk);
	void UpdatePan(const Track &trk);
	void UpdateTune(const Track &trk);
	void UpdateMod(const Track &trk);
	void UpdatePorta(const Track &trk, int sweepTicks);
	void Release();
	void Kill();
	void UpdateTrack();
	void Update();
	int32_t Interpolate();
	int32_t GenerateSample();
	// Advance by `ticks` sound-timer ticks (one output sample). A key-on that
	// is still pending (CS_START) does not run, like an unstarted channel.
	void IncrementSample(unsigned ticks);
	void clearHistory();
};
