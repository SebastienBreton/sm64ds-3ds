// Native port extension. Layout independently checked against ndspy's SSAR reader.
#include "SSAR.h"
#include <stdexcept>

uint32_t ReadSSAR(PseudoFile& file, uint32_t info, const FATSection& fat,
                  unsigned archive, unsigned sequence, SSEQ& out) {
    const auto* bytes=file.bytes();
    const auto total=file.size();
    auto range=[&](size_t at,size_t count) {
        if(at>total || count>total-at)
            throw std::range_error("SSAR outside SDAT");
    };
    auto u32=[&](size_t at){range(at,4);return ReadLE<uint32_t>(bytes+at);};
    auto u16=[&](size_t at){range(at,2);return ReadLE<uint16_t>(bytes+at);};
    const auto tableRel=u32(size_t(info)+12);
    if(!tableRel)throw std::range_error("No SSAR table");
    const size_t table=size_t(info)+tableRel;
    if(archive>=u32(table))throw std::range_error("SSAR archive index");
    const auto entryRel=u32(table+4+size_t(archive)*4);
    if(!entryRel)throw std::range_error("Absent SSAR archive");
    const auto fileID=u16(size_t(info)+entryRel);
    const size_t base=fat.records.at(fileID).offset;
    range(base,0x20);
    if(std::memcmp(bytes+base,"SSAR",4) || u32(base+4)!=0x0100feff ||
       std::memcmp(bytes+base+16,"DATA",4))
        throw std::runtime_error("Invalid SSAR header");
    const auto size=u32(base+8), data=u32(base+24), count=u32(base+28);
    range(base,size);
    if(size<32 || data>size || data<32 || count>(data-32)/12 || sequence>=count)
        throw std::range_error("Invalid SSAR entry table");
    const size_t entry=base+32+size_t(sequence)*12;
    const auto offset=u32(entry);
    if(offset==0xffffffff)throw std::range_error("Absent SSAR sequence");
    if(offset>=size-data)throw std::range_error("Invalid SSAR event offset");
    out.info.fileID=fileID;out.info.bank=u16(entry+4);out.info.vol=bytes[entry+6];
    out.filename="SSAR"+std::to_string(archive)+":"+std::to_string(sequence);
    // Keep the full region: jumps, calls and open-track offsets are archive-relative.
    out.data.assign(bytes+base+data,bytes+base+size);
    return offset;
}
