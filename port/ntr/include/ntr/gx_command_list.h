#pragma once
// Architectural prototype only: not yet connected to production GX ingress.
// This caches decoding, never transformed geometry or mutable GX state.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace ntr {
enum GxListDependency : std::uint32_t {
    GxMatrices=1u<<0, GxStacks=1u<<1, GxMatrixMode=1u<<2,
    GxVertexLatch=1u<<3, GxColor=1u<<4, GxTexcoord=1u<<5,
    GxMaterial=1u<<6, GxLights=1u<<7, GxShininess=1u<<8,
    GxViewport=1u<<9, GxTextureBinding=1u<<10, GxTextureMemory=1u<<11,
    GxPolygonAttributes=1u<<12, GxPrimitiveAssembly=1u<<13,
    GxAllDependencies=(1u<<14)-1
};
struct GxListDependencies {std::uint32_t reads=0,writes=0;};
inline GxListDependencies gx_list_dependencies(std::uint8_t cmd) {
    switch(cmd) {
        case 0x00:return {};
        case 0x10:return {0,GxMatrixMode|GxMatrices}; // derived matrix cache dirty flags
        case 0x11:case 0x13:return {GxMatrixMode|GxMatrices|GxStacks,GxStacks|GxMatrices};
        case 0x12:case 0x14:return {GxMatrixMode|GxStacks,GxStacks|GxMatrices};
        case 0x15:case 0x16:case 0x17:return {GxMatrixMode,GxMatrices};
        case 0x18:case 0x19:case 0x1a:case 0x1b:case 0x1c:
            return {GxMatrixMode|GxMatrices,GxMatrices};
        case 0x20:return {0,GxColor};
        case 0x21:return {GxMatrices|GxMaterial|GxLights|GxShininess|GxTextureBinding|GxTexcoord,
                         GxColor|GxTexcoord};
        case 0x22:return {GxMatrices|GxTextureBinding,GxTexcoord};
        case 0x23:case 0x24:case 0x25:case 0x26:case 0x27:case 0x28:
            return {GxMatrices|GxVertexLatch|GxColor|GxTexcoord|GxViewport|GxTextureBinding|GxPolygonAttributes|GxPrimitiveAssembly,
                    GxVertexLatch|GxTexcoord|GxPrimitiveAssembly};
        case 0x29:return {0,GxPolygonAttributes};
        case 0x2a:case 0x2b:return {GxTextureBinding|GxTextureMemory,GxTextureBinding};
        case 0x30:return {0,GxMaterial|GxColor};
        case 0x31:return {0,GxMaterial};
        case 0x32:return {GxMatrices,GxLights};
        case 0x33:return {0,GxLights};
        case 0x34:return {0,GxShininess};
        case 0x40:return {GxPolygonAttributes,GxPrimitiveAssembly|GxLights|GxPolygonAttributes};
        case 0x41:return {0,GxPrimitiveAssembly};
        case 0x50:return {GxAllDependencies,GxAllDependencies}; // future swap/result effects
        case 0x60:return {0,GxViewport};
        default:return {GxAllDependencies,GxAllDependencies};
    }
}

class GxCommandList {
public:
    struct Command {
        std::uint8_t opcode=0,parameter_count=0;
        std::uint16_t reserved=0;
        std::uint32_t parameter_offset=0,consumed_word_end=0;
    };
    static constexpr std::size_t max_words=16384,max_commands=32768;
private:
    std::vector<std::uint32_t> words;
    std::vector<Command> commands;
    GxListDependencies dependencies{};
    bool valid=false;
public:
    // Caller guarantees empty packed-decoder entry, a stable RAM span, and
    // original parameter counts. Failure publishes no executable partial list.
    template<class ParameterCount>
    bool compile(const std::uint32_t* input,std::size_t count,ParameterCount parameters) {
        valid=false;words.clear();commands.clear();dependencies={};
        if(count>max_words || (count && !input))return false;
        if(count)words.assign(input,input+count);
        std::size_t cursor=0;
        while(cursor<count) {
            const auto packed=words[cursor++];
            for(unsigned byte=0;byte<4;++byte) {
                const auto cmd=std::uint8_t(packed>>(8*byte));
                const int need=parameters(cmd);
                if(need<0 || need>32 || std::size_t(need)>count-cursor || commands.size()==max_commands){
                    words.clear();commands.clear();dependencies={};return false;
                }
                commands.push_back({cmd,std::uint8_t(need),0,std::uint32_t(cursor),std::uint32_t(cursor+need)});
                cursor+=need;
                const auto effects=gx_list_dependencies(cmd);
                dependencies.reads|=effects.reads;dependencies.writes|=effects.writes;
            }
        }
        valid=true;return true;
    }
    // Pointer identity is not a validity condition. Mutated bytes or a reused
    // allocation must fail, even when the address and length are unchanged.
    bool matches(const std::uint32_t* input,std::size_t count) const {
        return valid && count==words.size() && (!count || (input && std::memcmp(input,words.data(),count*sizeof(*input))==0));
    }
    template<class ConsumeWord,class Execute>
    bool replay(ConsumeWord consume,Execute execute) const {
        if(!valid)return false;
        std::size_t cursor=0;
        for(const auto& command:commands) {
            while(cursor<command.consumed_word_end)consume(words[cursor++]);
            execute(command.opcode,command.parameter_count?words.data()+command.parameter_offset:nullptr,
                    command.parameter_count);
        }
        return cursor==words.size();
    }
    const std::vector<Command>& decoded_commands() const {return commands;}
    GxListDependencies conservative_dependencies() const {return dependencies;}
    std::size_t byte_size() const {return words.size()*sizeof(std::uint32_t)+commands.size()*sizeof(Command);}
};
}
