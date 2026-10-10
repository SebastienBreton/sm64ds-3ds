#include "sound_commands.h"
#include <cstring>
#include <cstdio>
#include <limits>
Sm64dsSoundCommands::~Sm64dsSoundCommands(){sm64ds_mixer_close(mixer);}
void Sm64dsSoundCommands::setAlarmCallback(Sm64dsAlarmCallback callback,void* context){
    alarmCallback=callback;alarmContext=context;sm64ds_mixer_alarm_callback(mixer,callback,context);
}
Sm64dsSoundBuffer Sm64dsSoundCommands::find(std::uintptr_t address) const {
    for(const auto& r:resources){auto start=reinterpret_cast<std::uintptr_t>(r.data);
        if(r.data && address>=start && address-start<r.size)return {reinterpret_cast<const void*>(address),r.size-unsigned(address-start)};
    }
    return {};
}
bool Sm64dsSoundCommands::registerResource(Sm64dsSoundBuffer r){
    const auto start=reinterpret_cast<std::uintptr_t>(r.data);
    if(!r.data || !r.size || start>std::numeric_limits<std::uintptr_t>::max()-r.size){failure="invalid resource span";return false;}
    for(const auto& old:resources)if(old.data){
        auto base=reinterpret_cast<std::uintptr_t>(old.data);
        if(base==start && old.size==r.size)return true;
        if(start<base+old.size && base<start+r.size){failure="overlapping resource span";return false;}
    }
    for(auto& slot:resources)if(!slot.data){slot=r;return true;}
    failure="sound resource registry full";return false;
}
void Sm64dsSoundCommands::unregisterResource(const void* p){for(auto& r:resources)if(r.data==p){r={};return;}}
unsigned Sm64dsSoundCommands::activeMask() const {
    return sm64ds_mixer_active(mixer);
}
bool Sm64dsSoundCommands::ticks(unsigned player,std::uint32_t* value) const {
    return sm64ds_mixer_get_ticks(mixer,player,value);
}
bool Sm64dsSoundCommands::variable(unsigned player,unsigned index,std::int16_t* value) const {
    return sm64ds_mixer_get_variable(mixer,player,index,value);
}
// DS-native cadence (DS_NATIVE_RATE: one sample per 1024 ARM7 cycles,
// 32728.498 Hz = NDSP_SAMPLE_RATE): timers, sequencer and alarms run on the
// exact DS clock and the 3DS DSP plays the stream without resampling.
static constexpr unsigned MixerRate=32728;
bool Sm64dsSoundCommands::execute(unsigned command,const std::uintptr_t args[4]){
    failure="";
    if(!args){failure="missing sound arguments";return false;}
    if(command==9||command==10||command==0xb||command==0xe||command==0xf||command==0x13||command==0x15){
        if(!mixer)mixer=sm64ds_mixer_open(MixerRate);
        sm64ds_mixer_alarm_callback(mixer,alarmCallback,alarmContext);
        if(sm64ds_mixer_pcm_command(mixer,command,args))return true;
        failure=sm64ds_sseq_error();return false;
    }
    if(command==0x16 || command==0x17){
        if(!mixer)mixer=sm64ds_mixer_open(MixerRate);
        if(sm64ds_mixer_lock_channels(mixer,unsigned(args[0]),unsigned(args[1]),command==0x16))return true;
        failure=sm64ds_sseq_error();return false;
    }
    if(command>=0x1b && command<=0x1d){
        if(!sm64ds_mixer_invalidate(mixer,command,args[0],args[1])){failure=sm64ds_sseq_error();return false;}
        for(auto& resource:resources)if(resource.data){
            auto start=reinterpret_cast<std::uintptr_t>(resource.data);
            if(start>=args[0] && start<=args[1] && resource.size-1<=args[1]-start){sm64ds_mixer_forget_resource(mixer,resource.data);resource={};}
        }
        return true;
    }
    if(command==7||command==8){
        const auto id=command==7?args[0]:0;
        const auto index=command==7?args[1]:args[0];
        if(id>=16||index>=16){failure="invalid sound variable";return false;}
        if(!mixer)mixer=sm64ds_mixer_open(MixerRate);
        if(mixer&&sm64ds_mixer_set_variable(mixer,unsigned(id),unsigned(index)+(command==8?16:0),static_cast<int16_t>(command==7?args[2]:args[1])))return true;
        failure=sm64ds_sseq_error();return false;
    }
    const auto player=command==4?(args[0]&0x00ffffffU):args[0];
    if(player>=16){failure="invalid sound player";return false;}
    const unsigned id=player;
    if(command==4){
        if(sm64ds_mixer_track_parameter(mixer,id,args[1],args[2],args[3],unsigned(args[0]>>24)&255))return true;
        failure=sm64ds_sseq_error();return false;
    }
    if(command==1){if(!mixer)return true;if(sm64ds_mixer_stop(mixer,id))return true;failure=sm64ds_sseq_error();return false;}
    if(command==2){if(!mixer)return true;if(sm64ds_mixer_pause(mixer,id,args[1]!=0))return true;failure=sm64ds_sseq_error();return false;}
    if(command==3){if(sm64ds_mixer_parameter(mixer,id,args[1],args[2],args[3]))return true;failure=sm64ds_sseq_error();return false;}
    if(command==5){if(sm64ds_mixer_track_mute(mixer,id,args[1],args[2]))return true;failure=sm64ds_sseq_error();return false;}
    if(command==6){if(sm64ds_mixer_track_channels(mixer,id,args[1],args[2]))return true;failure=sm64ds_sseq_error();return false;}
    if(command!=0){failure="unsupported sound command";return false;}
    auto events=find(args[1]),bank=find(args[3]);
    if(!events.data || !bank.data || bank.size<0x38 || args[2]>=events.size){failure="unregistered/truncated sequence or bank";return false;}
    Sm64dsSoundBuffer waves[4]{};
    const auto* b=static_cast<const std::uint8_t*>(bank.data);
    for(unsigned i=0;i<4;++i){
        // Original ARM9/ARM7 SBNK runtime binding slots contain 32-bit pointers.
        std::uint32_t address;std::memcpy(&address,b+0x18+i*8,4);
        if(address){waves[i]=find(address);if(!waves[i].data){failure="unregistered bound wave archive";return false;}}
    }
    if(!mixer)mixer=sm64ds_mixer_open(MixerRate);
    if(!mixer||!sm64ds_mixer_start(mixer,id,events,unsigned(args[2]),bank,waves,127)){failure=sm64ds_sseq_error();return false;}
    return true;
}
void Sm64dsSoundCommands::prepare(unsigned command,const std::uintptr_t args[4]){
    if(command!=0||!mixer||!args)return;
    auto events=find(args[1]),bank=find(args[3]);
    if(!events.data||!bank.data||bank.size<0x38||args[2]>=events.size)return;
    Sm64dsSoundBuffer waves[4]{};
    const auto* b=static_cast<const std::uint8_t*>(bank.data);
    for(unsigned i=0;i<4;++i){std::uint32_t address;std::memcpy(&address,b+0x18+i*8,4);if(address)waves[i]=find(address);}
    sm64ds_mixer_prepare(mixer,events,unsigned(args[2]),bank,waves);
}
bool Sm64dsSoundCommands::render(std::int16_t* stereo,unsigned frames){
    if((!stereo&&frames)||frames>48000){failure="invalid render target";return false;}
    if(!mixer){if(frames)std::memset(stereo,0,frames*4);return true;}
    if(!sm64ds_mixer_render(mixer,stereo,frames)){failure=sm64ds_sseq_error();return false;}
    return true;
}

void Sm64dsSoundCommands::publishStatus(Sm64dsSoundStatus& status) const {
    status.activePlayers=activeMask();
    status.enabledChannels=sm64ds_mixer_enabled_channels(mixer);
    status.activeCaptures=sm64ds_mixer_captures(mixer);
    // One bulk copy instead of 272 checked getters (runs after every pump).
    std::int16_t locals[16][16];std::uint32_t tickValues[16];
    sm64ds_mixer_copy_status(mixer,locals,tickValues,status.globals);
    for(unsigned id=0;id<16;++id){
        status.players[id].ticks=tickValues[id];
        std::memcpy(status.players[id].variables,locals[id],sizeof locals[id]);
    }
}

bool Sm64dsSoundQueue::submit(std::uintptr_t head){
    if(failed)return false;
    failure="";
    if(!pool||!count||count>256||!head){failure="invalid sound command pool/head";return false;}
    const auto base=reinterpret_cast<std::uintptr_t>(pool);
    unsigned short order[256];bool seen[256]{};unsigned length=0;
    bool willBind=bound;
    // Validate the entire linked list before executing any of it. All nodes
    // stay in caller-owned memory, preserving links for original recycling.
    while(head){
        if(head<base||(head-base)%sizeof(Sm64dsSoundNode)||(head-base)/sizeof(Sm64dsSoundNode)>=count){failure="sound node outside command pool";return false;}
        const auto index=unsigned((head-base)/sizeof(Sm64dsSoundNode));
        if(seen[index]){failure="cyclic sound command list";return false;}
        seen[index]=true;order[length++]=index;
        const auto& node=pool[index];
        if(node.command==0x19){
            if(std::uintptr_t(node.args[0])!=reinterpret_cast<std::uintptr_t>(&status)){failure="unexpected shared sound status address";return false;}
            willBind=true;
        }else if(node.command>0xb && node.command!=0xe && node.command!=0xf && node.command!=0x13 && node.command!=0x15 && node.command!=0x16 && node.command!=0x17 && !(node.command>=0x1b && node.command<=0x1d)){
            std::snprintf(failureDetails,sizeof failureDetails,"unsupported queued sound command 0x%x args=%08x,%08x,%08x,%08x",node.command,node.args[0],node.args[1],node.args[2],node.args[3]);
            failure=failureDetails;return false;
        }
        head=node.next;
    }
    if(!willBind){failure="sound status not initialized";return false;}
    for(unsigned i=0;i<length;++i){
        const auto& node=pool[order[i]];
        if(node.command==0x19){bound=true;continue;}
        const std::uintptr_t args[4]={node.args[0],node.args[1],node.args[2],node.args[3]};
        if(!service.execute(node.command,args)){failed=true;failure=service.error();return false;}
    }
    service.releasePrepared();
    service.publishStatus(status);
    ++status.completedBatches;
    return true;
}
void Sm64dsSoundQueue::prepare(std::uintptr_t head){
    if(failed||!pool||!count||count>256)return;
    const auto base=reinterpret_cast<std::uintptr_t>(pool);
    for(unsigned steps=0;head&&steps<count;++steps){
        if(head<base||(head-base)%sizeof(Sm64dsSoundNode)||(head-base)/sizeof(Sm64dsSoundNode)>=count)return;
        const auto& node=pool[(head-base)/sizeof(Sm64dsSoundNode)];
        if(node.command==0){const std::uintptr_t args[4]={node.args[0],node.args[1],node.args[2],node.args[3]};service.prepare(0,args);}
        head=node.next;
    }
}
bool Sm64dsSoundQueue::refresh(){
    if(failed)return false;
    if(!bound){failure="sound status not initialized";return false;}
    service.publishStatus(status);return true;
}
