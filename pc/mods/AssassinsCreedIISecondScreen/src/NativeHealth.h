#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"

namespace ac2ds::native {
struct HealthState {
    uint32_t current{},maximum{},unavailable{};
    uint32_t RestorableMaximum()const{return maximum-unavailable;}
};
// Native subscribers015fde10,
// 015fde30 and015fde70 read the same owner's +58/+5c/+60 respectively.
// 015fde50 subtracts +60 from +5c; medicine must not restore that unavailable
// portion by directly assigning health. No engine function is invoked here.
inline std::optional<HealthState> ReadHealth(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    auto player=ResolvePlayerInventory(memory,roots.player);if(!player)return {};
    uint32_t registry{},events{},records{},count{},owner{};
    if(!memory.Member(player->context,0x20,registry) || !memory.Member(registry,0x4c,events) ||
        !memory.Member(events,0x14,records) || !memory.Member(events,0x38,count) || !count || count>4096 ||
        records<0x10000 || records>UINT32_MAX-count*24)return {};
    uint32_t found{};
    for(uint32_t i=0;i<count;++i){
        std::array<uint32_t,6> row{};if(!memory.Read(records+i*24,row))return {};
        if(row[1]&0x80000000u)continue;
        uint32_t bit=row[5]==0x15fde10?1:row[5]==0x15fde30?2:row[5]==0x15fde70?4:0;
        if(!bit)continue;
        uint32_t candidate{},type{},context{};
        // A row's callback receives row+8 in ECX only for kind5. Each verified
        // callback dereferences its interface+c to reach CSrvPlayerHealth.
        if((found&bit) || (row[1]&7)!=5 || !memory.Member(row[2],0xc,candidate) ||
            !memory.Read(candidate,type) || type!=0x1ea1658 ||
            !memory.Member(candidate,0x20,context) || context!=player->context ||
            (owner && owner!=candidate))return {};
        found|=bit;owner=candidate;
    }
    HealthState result;
    if(found!=7 || !memory.Member(owner,0x58,result.current) || !memory.Member(owner,0x5c,result.maximum) ||
        !memory.Member(owner,0x60,result.unavailable) || !result.maximum || result.maximum>4096 ||
        result.current>result.maximum || result.unavailable>result.maximum || !memory.Validate())return {};
    return result;
}
}
