// Pure planning layer: no Citro3D calls and no rendering side effects.
#ifndef SM64DS_SHADOW_STENCIL_PLAN_H
#define SM64DS_SHADOW_STENCIL_PLAN_H
#include <cstddef>
#include <cstdint>
#include <array>
namespace ds_shadow {
enum class Class : uint8_t { Opaque, Translucent };
enum class Failure : uint8_t { None, Capacity, Wireframe, DepthEqual };
enum class Test : uint8_t { Always, Equal };
enum class Op : uint8_t { Keep, Zero, Replace, Invert };
enum class Alpha : uint8_t { Disabled, OpaqueOnly, Nonzero };
enum class Depth : uint8_t { Disabled, SceneStrict };
enum class Blend : uint8_t { Replace, StraightAlphaMax };
struct Batch {
    uint32_t attr;
    // Effective alpha classes AFTER polygon/texture alpha modulation.
    bool opaque_texels;
    bool partial_texels;
};
struct Pass {
    bool fullscreen=false;
    Test test=Test::Always;
    uint8_t reference=0,input_mask=255,write_mask=0;
    Op stencil_fail=Op::Keep,depth_fail=Op::Keep,depth_pass=Op::Keep;
    Alpha alpha=Alpha::Disabled;
    Depth depth=Depth::SceneStrict;
    Blend blend=Blend::Replace;
    bool color_write=false,depth_write=false;
};
struct Sequence { bool previous_mask=false; }; // fresh instance for EACH eye
class FramePlan {
    std::array<uint8_t,128> tags_{};
    std::array<uint8_t,128> states_{};
    uint8_t count_=0,initial_tag_=0,empty_tag_=0;
    bool ready_=false;
    Failure failure_=Failure::None;
    static unsigned key(Class c,unsigned id){return (c==Class::Translucent?64:0)|(id&63);}
    bool add(Class c,unsigned id){
        unsigned k=key(c,id);if(tags_[k])return true;
        if(count_==127){failure_=Failure::Capacity;return false;}
        tags_[k]=++count_;states_[count_]=uint8_t(k);return true;
    }
public:
    // MUST succeed for the whole frame before clearing/drawing EITHER eye.
    // On false caller must choose CPU composition for the entire frame.
    bool prepare(const Batch* batches,std::size_t count,unsigned clear_id=64,bool clear_occupied=false){
        tags_.fill(0);states_.fill(0);count_=0;ready_=false;failure_=Failure::None;
        initial_tag_=empty_tag_=0;
        if(clear_id<64){
            if(clear_occupied){
                if(!add(Class::Opaque,clear_id))return false;
                initial_tag_=tag(Class::Opaque,clear_id);
            }else{
                // Transparent clear carries an opaque polygon ID but is not
                // occupied. It must not alias a later opaque polygon of that
                // same ID, which needs blending rather than replacement.
                initial_tag_=empty_tag_=++count_;
                states_[initial_tag_]=uint8_t(128|(clear_id&63));
            }
        }
        for(std::size_t i=0;i<count;++i){
            const auto& b=batches[i];if(!(b.attr&0xc0))continue;
            if(((b.attr>>16)&31)==0){failure_=Failure::Wireframe;return false;}
            // Nonzero-after-opaque relies on STRICT depth rejection. Do not
            // silently apply it to DS depth-equal polygons.
            if(b.attr&(1u<<14)){failure_=Failure::DepthEqual;return false;}
            unsigned id=(b.attr>>24)&63;bool shadow=((b.attr>>4)&3)==3;
            if(shadow){if(id && !add(Class::Translucent,id))return false;}
            else {
                if(b.opaque_texels && !add(Class::Opaque,id))return false;
                if(b.partial_texels && !add(Class::Translucent,id))return false;
            }
        }
        ready_=true;return true;
    }
    uint8_t initial_tag()const{return initial_tag_;}
    Failure failure()const{return failure_;}
    bool ready()const{return ready_;}
    unsigned count()const{return count_;}
    uint8_t tag(Class c,unsigned id)const{return tags_[key(c,id)];}
    // Caller must emit the same preflighted stream, preserving order, faces,
    // geometry and texture state. Each callback is one draw of that geometry,
    // except fullscreen=true which draws a coverage quad with culling off.
    template<class Emit> bool emit(const Batch& b,Sequence& seq,Emit emit)const{
        if(!ready_)return false;
        if(!(b.attr&0xc0))return true;
        unsigned id=(b.attr>>24)&63;bool shadow=((b.attr>>4)&3)==3;
        bool mask=shadow && id==0;
        if(!mask && ((shadow || b.partial_texels) && !tag(Class::Translucent,id)))return false;
        if(!shadow && b.opaque_texels && !tag(Class::Opaque,id))return false;
        if(mask){
            if(!seq.previous_mask){
                Pass clear;clear.fullscreen=true;clear.depth=Depth::Disabled;
                clear.write_mask=128;clear.depth_pass=Op::Zero;emit(clear);
            }
            seq.previous_mask=true;
            Pass p;p.reference=128;p.write_mask=128;p.depth_fail=Op::Replace;
            emit(p);return true; // no alpha/texture rejection, color or depth writes
        }
        seq.previous_mask=false;
        if(!shadow && b.opaque_texels){
            Pass p;p.reference=tag(Class::Opaque,id);p.write_mask=127;
            p.depth_pass=Op::Replace;p.alpha=Alpha::OpaqueOnly;
            p.color_write=p.depth_write=true;emit(p);
        }
        if(!shadow && !b.partial_texels)return true;
        const uint8_t target=tag(Class::Translucent,id);
        if(!target)return false; // caller violated prepare/emit stream contract
        auto transition=[&](uint8_t old,bool empty){
            Pass p;p.test=Test::Equal;p.reference=old|(shadow?128:0);
            p.input_mask=shadow?255:127;p.write_mask=old^target;
            p.depth_pass=Op::Invert;p.alpha=Alpha::Nonzero;
            p.color_write=true;p.depth_write=(b.attr&(1u<<11))!=0;
            p.blend=empty?Blend::Replace:Blend::StraightAlphaMax;emit(p);
        };
        if(!initial_tag_)transition(0,true);
        for(unsigned old=1;old<=count_;++old){
            unsigned state=states_[old];bool trans=(state&64)!=0;
            if((state&63)==id && (shadow || trans))continue;
            transition(uint8_t(old),old==empty_tag_);
        }
        return true;
    }
};
} // namespace ds_shadow
#endif
