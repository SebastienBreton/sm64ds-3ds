#pragma once
#include "ntr/gx_geometry_packet.h"
namespace ntr {
// Configure once before the first GX frame; never switch a live primitive or lease.
void gx_direct_configure(bool enabled);
bool gx_direct_enabled();
const gx_geometry::Packet& gx_direct_packet();
std::size_t gx_direct_triangle_count();
}
