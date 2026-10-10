#pragma once
#include "platform/3ds/native_mesh_packet.h"
#include <array>
#include <cstring>
#include <cstdint>

// Private lowering experiment. The only persistent geometry is the existing
// native_mesh::Planner output. Ops and bindings describe dynamic state, not a
// second expanded triangle/vertex representation. No SDK/DMA/IRQ entry is hooked.
namespace bmd_program {
using ntr::HostVector;
enum class Refusal { None, Empty, UnknownCommand, PartialParameters,
    PrimitiveCarry, PrimitiveSize, RelativeEntry, Capacity };
struct Op {
    uint8_t command=0;
    uint32_t parameter=0, source=0, matrix=0;
    uint32_t completion_words=0;
    uint8_t source_command=0;
};
struct DecoderExit {
    std::array<uint8_t,4> queue{};
    std::array<uint32_t,2> params{};
    unsigned params_written=0;
    unsigned queued=0,qpos=4,have=0,words_consumed=0;
};
struct ChunkCursor {
    size_t next_op=0,checkpoint=0;
    unsigned words_consumed=0;
    bool begun=false,finished=false;
};
class Program {
    HostVector<uint32_t> source_words_;
    HostVector<Op> ops_;
    HostVector<unsigned> binding_keys_;
    HostVector<HostVector<unsigned>> outputs_;
    DecoderExit decoder_;
    HostVector<DecoderExit> checkpoints_;
    uint64_t compilation_=0;
    unsigned source_vertices_=0, source_triangles_=0;
public:
    native_mesh::Planner layout;
    Refusal refusal=Refusal::None;
    uint8_t refused_command=0; // diagnostics (gpu-geometry-ingame): first unsupported command byte
    uint64_t compilations() const {return compilation_;}
    const auto& operations()const{return ops_;}
    const auto& bindings()const{return binding_keys_;}
    const auto& output_slots(unsigned source)const{return outputs_.at(source);}
    const auto& source_position(unsigned source)const{
        return layout.positions.at(outputs_.at(source).at(0));
    }
    const auto& decoder_exit()const{return decoder_;}
    const auto& checkpoints()const{return checkpoints_;}
    const auto& words()const{return source_words_;}
    unsigned source_vertices()const{return source_vertices_;}
    unsigned source_triangles()const{return source_triangles_;}
    size_t capacity_bytes()const{
        size_t bytes=source_words_.capacity()*sizeof(uint32_t)+ops_.capacity()*sizeof(Op)+
            binding_keys_.capacity()*sizeof(unsigned)+outputs_.capacity()*sizeof(HostVector<unsigned>)+
            checkpoints_.capacity()*sizeof(DecoderExit)+layout.positions.capacity()*sizeof(native_mesh::Position)+
            layout.attributes.capacity()*sizeof(native_mesh::Attribute)+layout.indices.capacity()*sizeof(uint16_t)+
            layout.runs.capacity()*sizeof(native_mesh::Run);
        for(const auto& output:outputs_)bytes+=output.capacity()*sizeof(unsigned);
        // Planner's private compile scratch cache is intentionally excluded;
        // expose that exclusion instead of calling this a heap footprint.
        return bytes;
    }
    bool matches(const uint32_t* words,size_t count)const{
        return refusal==Refusal::None && source_words_.size()==count &&
            (!count || !std::memcmp(words,source_words_.data(),count*4));
    }
    // Conservative boundary until an owned-asset mutation generation is proven.
    // This exact comparison remains part of the future measured hit cost.
    bool refresh(const uint32_t* words,size_t count){
        return matches(words,count) || compile(words,count);
    }
    bool compile(const uint32_t* words,size_t count){
        ++compilation_;refusal=Refusal::None;source_words_.clear();ops_.clear();
        binding_keys_.clear();outputs_.clear();decoder_={};checkpoints_.clear();
        source_vertices_=source_triangles_=0;
        auto fail=[&](Refusal why){refusal=why;return false;};
        if(!words || !count)return fail(Refusal::Empty);
        native_mesh::Packet packet;packet.clear(true);
        native_mesh::Material material{};material.polygon_attr=0x001f00c0;
        unsigned binding=32, current_matrix=0;bool matrix_bound=false;
        int16_t vx=0,vy=0,vz=0;bool position_known=false;
        int primitive=-1;HostVector<uint32_t> strip;
        auto matrix=[&](){
            // Unique logical bindings ensure layout does not assume that two
            // mutable DS slots happen to contain equal matrices on this frame.
            int32_t m[16]{};m[0]=m[5]=m[10]=m[15]=4096;
            m[12]=int32_t(binding+1)*4096;
            current_matrix=packet.matrix(m);
            while(binding_keys_.size()<=current_matrix)binding_keys_.push_back(binding);
            matrix_bound=true;
        };
        auto triangle=[&](uint32_t a,uint32_t b,uint32_t c){
            packet.triangle(a,b,c,material);++source_triangles_;
        };
        auto vertex=[&](){
            if(!matrix_bound)matrix();
            const auto id=packet.vertex({vx,vy,vz,source_vertices_,0,0,current_matrix,0,0,256,192});
            ops_.push_back({0xff,0,source_vertices_++,current_matrix});
            strip.push_back(id);const auto n=strip.size();
            switch(primitive){
            case 0:if(n==3){triangle(strip[0],strip[1],strip[2]);strip.clear();}break;
            case 1:if(n==4){triangle(strip[0],strip[1],strip[2]);triangle(strip[0],strip[2],strip[3]);strip.clear();}break;
            case 2:if(n>=3){if((n-3)&1)triangle(strip[n-2],strip[n-3],strip[n-1]);else triangle(strip[n-3],strip[n-2],strip[n-1]);}break;
            case 3:if(n>=4 && !(n%2)){triangle(strip[n-4],strip[n-3],strip[n-1]);triangle(strip[n-4],strip[n-1],strip[n-2]);}break;
            }
        };
        auto close_ok=[&](){
            if(primitive==0 || primitive==1)return strip.empty();
            if(primitive==2)return strip.size()>=3;
            if(primitive==3)return strip.size()>=4 && !(strip.size()%2);
            return false;
        };
        size_t at=0;
        while(at<count){
            const uint32_t packed=words[at++];
            for(unsigned i=0;i<4;++i)decoder_.queue[i]=uint8_t(packed>>(8*i));
            for(unsigned i=0;i<4;++i){
                const auto command=decoder_.queue[i];
                const unsigned n=command==0 || command==0x41?0:command==0x23?2:1;
                if(command!=0 && command!=0x14 && !(command>=0x20 && command<=0x28) && command!=0x40 && command!=0x41)
                    return (refused_command=command,fail(Refusal::UnknownCommand));
                if(at+n>count)return fail(Refusal::PartialParameters);
                const uint32_t p=n?words[at]:0, p1=n==2?words[at+1]:0;at+=n;
                for(unsigned k=0;k<n;++k)decoder_.params[k]=words[at-n+k];
                decoder_.params_written=std::max(decoder_.params_written,n);
                if(command==0)continue;
                if(command==0x14){binding=p&31;matrix_bound=false;ops_.push_back({command,p});continue;}
                if(command==0x40){
                    if(primitive!=-1)return fail(Refusal::PrimitiveCarry);
                    primitive=p&3;strip.clear();ops_.push_back({command,p});continue;
                }
                if(command==0x41){
                    if(!close_ok())return fail(Refusal::PrimitiveSize);
                    primitive=-1;strip.clear();ops_.push_back({command});continue;
                }
                if(command<=0x22){ops_.push_back({command,p});continue;}
                if(primitive==-1)return fail(Refusal::PrimitiveCarry);
                if(command==0x23){vx=int16_t(p);vy=int16_t(p>>16);vz=int16_t(p1);position_known=true;}
                else if(command==0x24){
                    auto s10=[](uint32_t v){int x=int(v&1023);if(x>=512)x-=1024;return int16_t(x*64);};
                    vx=s10(p);vy=s10(p>>10);vz=s10(p>>20);position_known=true;
                }else{
                    if(!position_known)return fail(Refusal::RelativeEntry);
                    if(command==0x25){vx=int16_t(p);vy=int16_t(p>>16);}
                    else if(command==0x26){vx=int16_t(p);vz=int16_t(p>>16);}
                    else if(command==0x27){vy=int16_t(p);vz=int16_t(p>>16);}
                    else {auto d10=[](uint32_t v){int x=int(v&1023);return x>=512?x-1024:x;};
                        vx=int16_t(int(vx)+d10(p));vy=int16_t(int(vy)+d10(p>>10));vz=int16_t(int(vz)+d10(p>>20));}
                }
                vertex();
            }
        }
        if(primitive!=-1)return fail(Refusal::PrimitiveCarry);
        if(!layout.build(packet))return fail(Refusal::Capacity);
        outputs_.resize(source_vertices_);
        for(unsigned i=0;i<layout.attributes.size();++i){
            const unsigned source=layout.attributes[i].color;
            if(source>=outputs_.size())return fail(Refusal::Capacity);
            outputs_[source].push_back(i);
        }
        for(const auto& outputs:outputs_)if(outputs.empty())return fail(Refusal::PrimitiveSize);
        // Compile decoder prefixes once. A DMA chunk may end after the first
        // VTX_16 parameter; zero-param ops share the preceding completion word.
        // Checkpoint params are only the two slots written by this command class.
        // Slots2..31 and all idle entry leftovers remain owned by the evaluator.
        DecoderExit prefix;size_t next_op=0;
        auto parameter_count=[](uint8_t c){return c==0 || c==0x41?0u:c==0x23?2u:1u;};
        auto completed=[&](uint8_t c){
            if(c){
                if(next_op>=ops_.size())return false;
                auto& op=ops_[next_op++];
                if(op.command!=(c>=0x23 && c<=0x28?0xff:c))return false;
                op.completion_words=prefix.words_consumed;op.source_command=c;
            }
            return true;
        };
        auto advance_zero=[&](){
            while(prefix.qpos<prefix.queued && parameter_count(prefix.queue[prefix.qpos])==0){
                if(!completed(prefix.queue[prefix.qpos]))return false;
                ++prefix.qpos;
            }
            if(prefix.qpos>=prefix.queued)prefix.queued=0;
            return true;
        };
        for(unsigned i=0;i<count;++i){
            prefix.words_consumed=i+1;
            if(!prefix.queued){
                for(unsigned k=0;k<4;++k)prefix.queue[k]=uint8_t(words[i]>>(8*k));
                prefix.queued=4;prefix.qpos=prefix.have=0;
                if(!advance_zero())return fail(Refusal::Capacity);
            }else{
                prefix.params[prefix.have++]=words[i];
                prefix.params_written=std::max(prefix.params_written,prefix.have);
                if(prefix.have>=parameter_count(prefix.queue[prefix.qpos])){
                    if(!completed(prefix.queue[prefix.qpos]))return fail(Refusal::Capacity);
                    prefix.have=0;++prefix.qpos;
                    if(!advance_zero())return fail(Refusal::Capacity);
                }
            }
            if((i+1)%118==0 || i+1==count)checkpoints_.push_back(prefix);
        }
        if(next_op!=ops_.size() || prefix.queued || prefix.have)return fail(Refusal::PartialParameters);
        decoder_=prefix;source_words_.assign(words,words+count);return true;
    }
    // Dynamic state evaluator must implement exact original CPU lighting and
    // texgen. Vertex IDs go directly to existing Attribute output slots; no
    // Packet append, triangle expansion or Planner occurs on this call.
    template<class Evaluator> bool execute(Evaluator& evaluator){
        if(refusal!=Refusal::None || !evaluator.preflight(*this))return false;
        evaluator.begin(*this);
        for(const auto& op:ops_){
            if(op.command==0xff)evaluator.vertex(op,*this);
            else evaluator.state(op.command,op.parameter);
        }
        evaluator.finish(*this);return true;
    }
    // Exactly one original DMA chunk per call, before its IRQ/callback. The
    // caller keeps the SDK pump and DMA scheduling; no early whole-list replay.
    // A failed boundary check is non-mutating and must fall back before transfer.
    template<class Evaluator> bool begin_chunked(Evaluator& evaluator,ChunkCursor& cursor){
        if(cursor.begun || refusal!=Refusal::None || !evaluator.preflight(*this))return false;
        evaluator.begin(*this);cursor={};cursor.begun=true;return true;
    }
    template<class Evaluator> bool execute_chunk(Evaluator& evaluator,ChunkCursor& cursor,unsigned words_consumed){
        if(!cursor.begun || cursor.finished || cursor.checkpoint>=checkpoints_.size() ||
           checkpoints_[cursor.checkpoint].words_consumed!=words_consumed)return false;
        while(cursor.next_op<ops_.size() && ops_[cursor.next_op].completion_words<=words_consumed){
            const auto& op=ops_[cursor.next_op++];
            if(op.command==0xff)evaluator.vertex(op,*this);
            else evaluator.state(op.command,op.parameter);
        }
        evaluator.checkpoint(*this,checkpoints_[cursor.checkpoint++],cursor.words_consumed);
        cursor.words_consumed=words_consumed;
        if(words_consumed==source_words_.size()){
            evaluator.finish_chunked(*this);cursor.finished=true;
        }
        return true;
    }
};
}
