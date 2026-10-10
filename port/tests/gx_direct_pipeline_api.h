#pragma once
#include "gx_geometry_arena_candidate.h"
namespace ntr {
// Isolated, fixed for the whole process before original game setup.
void gx_direct_configure(bool enabled);
bool gx_direct_enabled();
const gx_arena_candidate::Packet& gx_direct_packet();
std::size_t gx_direct_triangle_count();
}
void player_direct_configure(bool reuse_between_presentations);
