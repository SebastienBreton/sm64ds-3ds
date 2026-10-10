/*
 * SSEQ Player - SDAT SWAV (Waveform/Sample) structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-09-08
 *
 * Nintendo DS Nitro Composer (SDAT) Specification document found at
 * http://www.feshrine.net/hacking/doc/nds-sdat.html
 */

#pragma once

#include "common.h"

// std::vector whose resize() leaves new elements uninitialised (each decoder
// writes every element), avoiding a zero fill before the overwrite.
template<typename T> struct DefaultInitAllocator : std::allocator<T>
{
	template<typename U> struct rebind { using other = DefaultInitAllocator<U>; };
	DefaultInitAllocator() = default;
	template<typename U> DefaultInitAllocator(const DefaultInitAllocator<U> &) {}
	template<typename U> void construct(U *p) { ::new (static_cast<void *>(p)) U; }
	template<typename U, typename... A> void construct(U *p, A &&...a) { ::new (static_cast<void *>(p)) U(std::forward<A>(a)...); }
};

struct SWAV
{
	uint8_t waveType;
	uint8_t loop;
	uint16_t sampleRate;
	uint16_t time;
	uint32_t loopOffset;
	uint32_t nonLoopLength;
	// Parsing only records where the encoded bytes are (the registered sound
	// resource stays valid while a bank can bind it). Prepare(), on the game
	// side when a bank that uses this wave starts, allocates the decoded
	// buffer (same allocator/heap as an eager decode) and parks the encoded
	// bytes at its tail; Samples() then decodes in place on first use, with no
	// allocation, so the realtime renderer may call it.
	mutable std::vector<int16_t, DefaultInitAllocator<int16_t>> data;
	mutable const int16_t *dataptr;
	const uint8_t *encoded;
	uint32_t encodedSize;
	mutable bool prepared;

	SWAV();

	void Read(PseudoFile &file);
	void Prepare() const;
	// nullptr when the wave was never prepared (the note is then skipped).
	const int16_t *Samples() const { if (!this->dataptr && this->prepared) this->Decode(); return this->dataptr; }
	void Decode() const;
	void DecodeADPCM(const uint8_t *origData, uint32_t len) const;
};
