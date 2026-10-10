#pragma once
#include <cstdint>
namespace bmd_native {
enum class SyncRefusal { None, IOUnavailable, Nested, Channel, Size, Alignment, AddressWrap, Busy, IRQ, SourceOutsideGameArena };
// The predicate describes the one contiguous original-game arena. Its memory
// is distinct from GX static state and live HostAllocator allocations. This
// certifies an execution domain, not allocation liveness or arbitrary pointers.
// Caller still validates immutable Program bytes and reserves all resources
// before installing a transfer; only the synchronous SDK entry may use this.
template<class Contains> SyncRefusal sync_admission(bool io_ready, bool active,
 int channel, uint32_t source, int bytes, uint32_t control, uint32_t ie, Contains contains) {
 if(!io_ready)return SyncRefusal::IOUnavailable;
 if(active)return SyncRefusal::Nested;
 if(channel<0 || channel>3)return SyncRefusal::Channel;
 if(bytes<=0 || bytes>0x100000)return SyncRefusal::Size;
 if((source&3u) || (unsigned(bytes)&3u))return SyncRefusal::Alignment;
 const uint64_t end=uint64_t(source)+unsigned(bytes);
 if(end>uint64_t(UINT32_MAX)+1)return SyncRefusal::AddressWrap;
 if(control&0x80000000u)return SyncRefusal::Busy;
 if(ie&0x00200000u)return SyncRefusal::IRQ;
 if(!contains(source) || !contains(uint32_t(end-1)))return SyncRefusal::SourceOutsideGameArena;
 return SyncRefusal::None;
}
}
