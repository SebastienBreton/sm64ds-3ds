#pragma once
#include "SSEQ.h"
#include "FATSection.h"
// Returns the entry offset into the shared sequence-event region.
uint32_t ReadSSAR(PseudoFile&, uint32_t infoOffset, const FATSection&,
                  unsigned archive, unsigned sequence, SSEQ&);
