/*
 * SSEQ Player - SDAT structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2014-09-08
 *
 * Nintendo DS Nitro Composer (SDAT) Specification document found at
 * http://www.feshrine.net/hacking/doc/nds-sdat.html
 */

#pragma once

#include <memory>
#include "SSEQ.h"
#include "SBNK.h"
#include "SWAR.h"
#include "common.h"

struct SDATBankState {
	uint32_t fileID = 0xffffffffU;
	uint16_t waveArc[4] = {0xffff,0xffff,0xffff,0xffff};
};

struct SDAT
{
	std::unique_ptr<SSEQ> sseq;
	std::unique_ptr<SBNK> sbnk;
	// Loaded native sequences can share immutable decoded wave archives.
	std::shared_ptr<SWAR> swar[4];

	uint32_t entryOffset = 0;
	SDAT() = default;
	SDAT(PseudoFile &file, uint32_t sseqToLoad, int archive = -1, int bankOverride = -1, SDATBankState* bankState = nullptr);
private:
	SDAT(const SDAT &);
	SDAT &operator=(const SDAT &);
};
