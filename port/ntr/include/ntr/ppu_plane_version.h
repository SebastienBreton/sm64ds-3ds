#pragma once
#include "ntr/ppu.h"
namespace ntr {
struct CandidatePlaneVersion {const void* identity=nullptr;uint64_t generation=0;};
// Certifies the current borrowed raw planes, never the separate composition
// state. The consumer must finish before preparing that engine again.
CandidatePlaneVersion ppu_candidate_plane_version(const PpuPreparedCandidates& view);
}
