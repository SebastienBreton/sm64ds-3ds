// The recovered header declares SpawnParticles overrides for these classes,
// but the ROM vtables inherit their base implementation (addresses below).
// Supply forwarding definitions so GCC emits native vtables and constructors
// use their native address points, without changing the byte-matching header.
#include "dPa_c.h"

// ROM slot 0: 0x02022640, simpleCallback_c::SpawnParticles.
void dPa_c::level_c::bubbleCallback_c::SpawnParticles(Particle::System& s) {
    simpleCallback_c::SpawnParticles(s);
}
void dPa_c::level_c::splashCallback_c::SpawnParticles(Particle::System& s) {
    simpleCallback_c::SpawnParticles(s);
}
void dPa_c::level_c::fitWaterSimpleCallback_c::SpawnParticles(Particle::System& s) {
    simpleCallback_c::SpawnParticles(s);
}

// ROM slot 0: 0x020226d0, callback_c::SpawnParticles.
void dPa_c::level_c::checkWaterCallback_c::SpawnParticles(Particle::System& s) {
    callback_c::SpawnParticles(s);
}
void dPa_c::level_c::checkWaterRippleCallback_c::SpawnParticles(Particle::System& s) {
    callback_c::SpawnParticles(s);
}
void dPa_c::level_c::clipCallback_c::SpawnParticles(Particle::System& s) {
    callback_c::SpawnParticles(s);
}
void dPa_c::level_c::fitWaterCallback_c::SpawnParticles(Particle::System& s) {
    callback_c::SpawnParticles(s);
}
void dPa_c::level_c::cleanParticleCallback_c::SpawnParticles(Particle::System& s) {
    callback_c::SpawnParticles(s);
}

static_assert(sizeof(dPa_c::level_c::callback_c) == 4, "callback ABI");
static_assert(sizeof(dPa_c::level_c::scaleCallback_c) == 12, "scale callback ABI");
static_assert(sizeof(dPa_c::level_c::clipCallback_c) == 8, "clip callback ABI");
