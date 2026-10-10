#pragma once
#include "ntr/bmd_draw_program.h"
#include <limits>

namespace bmd_program {
enum class ConsumeResult { NotRegistered, Consumed, WrongBoundary, SourceChanged, Failed };
// Private SDK adapter contract. This object never delivers IRQs, calls DMA or
// changes GX FIFO status. Original DMAStartTransfer calls consume BEFORE its
// original completion/half-empty callback. Caller keeps Program/Evaluator alive.
// Unexpected source mutation after a consumed prefix is an explicit capability
// failure: caller must stop, or migrate that prefix exactly before CPU fallback.
// It must never replay the current live tail through GX against missing geometry.
template<class Evaluator> class ChunkTransfer {
    Program* program_=nullptr;Evaluator* evaluator_=nullptr;
    const uint32_t* source_=nullptr;
    size_t count_=0;
    ChunkCursor cursor_;
    bool verified_=false;
public:
    const ChunkCursor& cursor()const{return cursor_;}
    bool active()const{return program_!=nullptr;}
    bool finished()const{return active() && cursor_.finished;}
    // verified: the caller compared every source word with the program during
    // this same synchronous submission; the static list cannot change before
    // its chunks are consumed, so the per-chunk comparisons are skipped.
    bool start(Program& program,Evaluator& evaluator,const uint32_t* source,size_t count,bool verified=false){
        if(active() || !source || !count || count>std::numeric_limits<unsigned>::max() ||
           count>std::numeric_limits<uintptr_t>::max()/4 ||
           reinterpret_cast<uintptr_t>(source)>std::numeric_limits<uintptr_t>::max()-count*4 ||
           (!verified && !program.matches(source,count)))return false;
        ChunkCursor cursor;if(!program.begin_chunked(evaluator,cursor))return false;
        program_=&program;evaluator_=&evaluator;source_=source;count_=count;cursor_=cursor;verified_=verified;return true;
    }
    ConsumeResult consume(const uint32_t* source,size_t words){
        if(!active())return ConsumeResult::NotRegistered;
        const auto begin=reinterpret_cast<uintptr_t>(source_),end=begin+count_*4;
        const auto supplied=reinterpret_cast<uintptr_t>(source);
        if(supplied<begin || supplied>=end)return ConsumeResult::NotRegistered;
        if(cursor_.finished || supplied!=begin+size_t(cursor_.words_consumed)*4 ||
           !words || words>count_-cursor_.words_consumed || cursor_.checkpoint>=program_->checkpoints().size() ||
           words!=program_->checkpoints()[cursor_.checkpoint].words_consumed-cursor_.words_consumed)
            return ConsumeResult::WrongBoundary;
        if(!verified_ && std::memcmp(source,program_->words().data()+cursor_.words_consumed,words*4))
            return ConsumeResult::SourceChanged;
        const unsigned consumed=cursor_.words_consumed+unsigned(words);
        if(!program_->execute_chunk(*evaluator_,cursor_,consumed))return ConsumeResult::Failed;
        return ConsumeResult::Consumed;
    }
    // Clearing this registration does not roll back state or retire any GPU lease.
    void clear(){program_=nullptr;evaluator_=nullptr;source_=nullptr;count_=0;cursor_={};verified_=false;}
};
}
