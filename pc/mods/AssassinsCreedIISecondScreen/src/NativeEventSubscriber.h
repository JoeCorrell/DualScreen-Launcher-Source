#pragma once
#include "NativeInventory.h"
namespace ac2ds::native {
struct EventDescriptor { uint32_t idGlobal{},callback{}; };
struct EventSubscriber { uint32_t interface{},owner{}; };
// Resolve only known native events in the player's existing registry. The
// caller owns the encompassing ReadTransaction and validates before use.
template<size_t N>
std::optional<EventSubscriber> ResolveEventSubscriber(const Memory& memory,uint32_t context,
        const std::array<EventDescriptor,N>& descriptors,uint32_t interfaceType,uint32_t ownerType){
    static_assert(N>0 && N<=32,"Bounded native event set");
    uint32_t registry{},events{},records{},count{};
    if(!memory.Member(context,0x20,registry) || !memory.Member(registry,0x4c,events) ||
        !memory.Member(events,0x14,records) || !memory.Member(events,0x38,count) || !count || count>4096 ||
        records<0x10000 || records>UINT32_MAX-count*24)return {};
    std::array<uint32_t,N> ids{};std::array<bool,N> found{};EventSubscriber result{};
    for(size_t i=0;i<N;++i){
        if(!memory.Read(descriptors[i].idGlobal,ids[i]) || !ids[i] || ids[i]>65535)return {};
        for(size_t j=0;j<i;++j)if(ids[i]==ids[j])return {};
    }
    for(uint32_t i=0;i<count;++i){
        std::array<uint32_t,6> row{};if(!memory.Read(records+i*24,row))return {};
        if(row[1]&0x80000000u)continue;
        auto event=(row[1]>>3)&0xffffu;
        for(size_t j=0;j<N;++j)if(event==ids[j]){
            if(found[j] || (row[1]&7)!=5 || row[5]!=descriptors[j].callback || row[2]<0x10000 ||
                (result.interface && result.interface!=row[2]))return {};
            found[j]=true;result.interface=row[2];
        }
    }
    uint32_t type{},ownerContext{};
    for(bool present:found)if(!present)return {};
    if(!memory.Read(result.interface,type) || type!=interfaceType ||
        !memory.Member(result.interface,0xc,result.owner) || !memory.Read(result.owner,type) || type!=ownerType ||
        !memory.Member(result.owner,0x20,ownerContext) || ownerContext!=context)return {};
    return result;
}
}
