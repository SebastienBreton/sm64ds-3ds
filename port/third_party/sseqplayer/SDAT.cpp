#include <algorithm>
/*
 * SSEQ Player - SDAT structure
 * By Naram Qashat (CyberBotX) [cyberbotx@cyberbotx.com]
 * Last modification on 2013-03-21
 *
 * Nintendo DS Nitro Composer (SDAT) Specification document found at
 * http://www.feshrine.net/hacking/doc/nds-sdat.html
 */

#include "SDAT.h"
#include "NDSStdHeader.h"
#include "SYMBSection.h"
#include "INFOSection.h"
#include "FATSection.h"
#include "convert.h"
#include "SSAR.h"

SDAT::SDAT(PseudoFile &file, uint32_t sseqToLoad, int archive, int bankOverride, SDATBankState* bankState) : sseq(), sbnk()
{
	// Read sections
	NDSStdHeader header;
	header.Read(file);
	header.Verify("SDAT", 0x0100FEFF);
	uint32_t SYMBOffset = file.ReadLE<uint32_t>();
	file.ReadLE<uint32_t>(); // SYMB size
	uint32_t INFOOffset = file.ReadLE<uint32_t>();
	file.ReadLE<uint32_t>(); // INFO size
	uint32_t FATOffset = file.ReadLE<uint32_t>();
	file.ReadLE<uint32_t>(); // FAT Size
	SYMBSection symbSection;
	if (SYMBOffset)
	{
		file.pos = SYMBOffset;
		symbSection.Read(file);
	}
	file.pos = INFOOffset;
	INFOSection infoSection;
	infoSection.Read(file);
	file.pos = FATOffset;
	FATSection fatSection;
	fatSection.Read(file);

	if (archive < 0 && infoSection.SEQrecord.entries.empty())
		throw std::logic_error("No SSEQ records found in SDAT");

	if (archive < 0 && !infoSection.SEQrecord.entries.count(sseqToLoad))
		throw std::range_error("SSEQ of " + stringify(sseqToLoad) + " is not found");

	// Read SSEQ
	if (archive >= 0 || infoSection.SEQrecord.entries.count(sseqToLoad))
	{
		uint16_t fileID = 0;
		std::string name;
		this->sseq = std::make_unique<SSEQ>();
		SSEQ *newSSEQ = this->sseq.get();
		if (archive >= 0)
			this->entryOffset = ReadSSAR(file, INFOOffset, fatSection, archive, sseqToLoad, *newSSEQ);
		else {
			fileID = infoSection.SEQrecord.entries[sseqToLoad].fileID;
			newSSEQ->filename = "SSEQ" + NumToHexString(fileID).substr(2);
			if (SYMBOffset)
				newSSEQ->filename = NumToHexString(sseqToLoad).substr(6) + " - " + symbSection.SEQrecord.entries[sseqToLoad];
			file.pos = fatSection.records.at(fileID).offset;
			newSSEQ->info = infoSection.SEQrecord.entries[sseqToLoad];
			newSSEQ->Read(file);
		}

		// Read SBNK for this SSEQ
		if (bankOverride >= 0) newSSEQ->info.bank = bankOverride;
		uint16_t bank = newSSEQ->info.bank;
		if (!infoSection.BANKrecord.entries.count(bank))
			throw std::range_error("Sequence bank is absent; caller must supply the scene bank");
		fileID = infoSection.BANKrecord.entries[bank].fileID;
		name = "SBNK" + NumToHexString(fileID).substr(2);
		if (SYMBOffset)
			name = NumToHexString(bank).substr(2) + " - " + symbSection.BANKrecord.entries[bank];
		file.pos = fatSection.records.at(fileID).offset;
		this->sbnk = std::make_unique<SBNK>(name);
		SBNK *newSBNK = this->sbnk.get();
		newSSEQ->bank = newSBNK;
		newSBNK->info = infoSection.BANKrecord.entries[bank];
		// The game caches SBNK files by file ID. An alias changes only non-FFFF slots.
		if (bankState && bankState->fileID == newSBNK->info.fileID)
			for (unsigned i=0;i<4;++i)
				if (newSBNK->info.waveArc[i] == 0xffff)
					newSBNK->info.waveArc[i] = bankState->waveArc[i];
		newSBNK->Read(file);

		// Read SWARs for this SBNK
		for (int i = 0; i < 4; ++i)
			if (newSBNK->info.waveArc[i] != 0xFFFF)
			{
				uint16_t waveArc = newSBNK->info.waveArc[i];
				if (!infoSection.WAVEARCrecord.entries.count(waveArc))throw std::range_error("Bound wave archive is absent");
				fileID = infoSection.WAVEARCrecord.entries[waveArc].fileID;
				name = "SWAR" + NumToHexString(fileID).substr(2);
				if (SYMBOffset)
					name = NumToHexString(waveArc).substr(2) + " - " + symbSection.WAVEARCrecord.entries[waveArc];
				file.pos = fatSection.records.at(fileID).offset;
				this->swar[i] = std::make_unique<SWAR>(name);
				SWAR *newSWAR = this->swar[i].get();
				newSBNK->waveArc[i] = newSWAR;
				newSWAR->info = infoSection.WAVEARCrecord.entries[waveArc];
				newSWAR->Read(file);
				newSWAR->PrepareAll(); // the SDAT copy is released after parsing
			}
			else
				this->swar[i].reset();
		if (bankState) {
			bankState->fileID = newSBNK->info.fileID;
			std::copy_n(newSBNK->info.waveArc,4,bankState->waveArc);
		}
	}
}
