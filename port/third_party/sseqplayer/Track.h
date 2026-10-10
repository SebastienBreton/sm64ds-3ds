/*
 * SSEQ Player - Track structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-10-13
 *
 * Adapted from source code of FeOS Sound System
 * By fincs
 * https://github.com/fincs/FSS
 */

#pragma once

#include <functional>
#include <bitset>
#include "consts.h"

struct Player;

enum StackType
{
	STACKTYPE_CALL,
	STACKTYPE_LOOP
};

struct StackValue
{
	StackType type;
	const uint8_t *dest;

	StackValue() : type(STACKTYPE_CALL), dest(nullptr) { }
	StackValue(StackType newType, const uint8_t *newDest) : type(newType), dest(newDest) { }
};

struct Override
{
	bool overriding;
	int cmd;
	int value;
	int extraValue;

	Override() : overriding(false) { }
	bool operator()() const { return this->overriding; }
	bool &operator()() { return this->overriding; }
	int val(const uint8_t **pData, std::function<int (const uint8_t **)> reader, bool returnExtra = false)
	{
		if (this->overriding)
			return returnExtra ? this->extraValue : this->value;
		else
			return reader(pData);
	}
};

struct Track
{
	int8_t trackId;

	std::bitset<TS_BITS> state;
	uint8_t num, prio;
	Player *ply;

	const uint8_t *startPos;
	const uint8_t *pos;
	StackValue stack[FSS_TRACKSTACKSIZE];
	uint8_t stackPos;
	uint8_t loopCount[FSS_TRACKSTACKSIZE];
	Override overriding;
	bool lastComparisonResult;

	int wait;
	bool muted=false;
 uint16_t channelMask=0xffff;
 bool channelMaskExplicit=false;
	bool waitForNotes; // Nitro: a zero-duration note waits for the track channel list to empty.
	uint16_t patch;
	uint8_t portaKey, portaTime;
	int16_t sweepPitch;
	uint8_t vol, expr;
	int8_t pan; // -64..63
 int16_t externalVolume,externalTune;
 int8_t externalPan;
	uint8_t pitchBendRange;
	int8_t pitchBend;
	int8_t transpose;

	uint8_t a, d, s, r;

	uint8_t modType, modSpeed, modDepth, modRange;
	uint16_t modDelay;

	std::bitset<TUF_BITS> updateFlags;

	Track();

	void Init(uint8_t handle, Player *ply, const uint8_t *pos, int n);
	void Zero();
	void ClearState();
	void Free();
	int NoteOn(int key, int vel, int len, int sweepLen = -1);
	int NoteOnTie(int key, int vel, int len);
	int SweepTicks(int len) const;
	void ReleaseAllNotes(int releaseOverride = -1);
	void Run();
 static bool ParameterSupported(unsigned offset,unsigned width);
 void SetParameter(unsigned offset,unsigned value);
 void SetMute(unsigned mode);
 void SetChannelMask(unsigned mask){channelMask=static_cast<uint16_t>(mask);channelMaskExplicit=true;}
};
