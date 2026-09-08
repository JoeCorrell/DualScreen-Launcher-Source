#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
#include <algorithm>
namespace ac2ds::native {
struct Villa { bool unlocked{};uint32_t restored{},value{},population{}; };
// 0118bce0 gates StatsVilla through equipment group 4, menu 18, and the
// resolved definition's +30 lock flag. A value existing in a save is not unlock.
inline std::optional<bool> VillaUnlocked(const Memory& memory){
    uint32_t manager{},catalog{},group{};PointerArray catalogs{},groups{},entries{};
    if(!memory.Read(0x223f81c,manager) || !ArrayHeader(memory,manager,0xc,64,catalogs))return {};
    for(uint32_t i=0;i<catalogs.Count();++i){
        uint32_t candidate{},type{};
        if(!memory.Field(catalogs.data,i*4,candidate))return {};
        if(!candidate)continue;
        if(!memory.Read(candidate,type))return {};
        if(type==0x1e610c8){if(catalog)return {};catalog=candidate;}
    }
    if(!catalog)return false;
    if(!ArrayHeader(memory,catalog,8,32,groups))return {};
    for(uint32_t i=0;i<groups.Count();++i){
        uint32_t handle{},candidate{},id{};
        if(!memory.Field(groups.data,i*4,handle) || !memory.Reference(handle,candidate))return {};
        if(!candidate)continue;
        if(!memory.Member(candidate,0x10,id))return {};
        if(id==4){group=candidate;break;} // Native 0115a210 selects first match.
    }
    if(!group)return false;
    if(!ArrayHeader(memory,group,8,128,entries))return {};
    for(uint32_t i=0;i<entries.Count();++i){
        uint32_t handle{},entry{},type{},menu{};
        if(!memory.Field(entries.data,i*4,handle) || !memory.Reference(handle,entry))return {};
        if(!entry)continue;
        if(!memory.Read(entry,type) || type!=0x1e61020 || !memory.Member(entry,0x14,menu))return {};
        if(menu!=18)continue;
        uint32_t definition{};uint8_t locked{};
        if(!memory.Member(entry,8,handle) || !memory.Reference(handle,definition))return {};
        if(!definition)return false;
        if(!memory.Read(definition,type) || type!=0x1e699bc || !memory.Member(definition,0x30,locked))return {};
        return locked==0;
    }
    return false;
}
inline std::optional<Villa> ReadVilla(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    auto unlocked=VillaUnlocked(memory);if(!unlocked)return {};
    Villa result;result.unlocked=*unlocked;
    if(result.unlocked){
        uint32_t type{},maximum{};
        if(!memory.Read(roots.progress,type) || type!=0x1e5dbd0 ||
            !memory.Member(roots.progress,0xc,result.value) || !memory.Member(roots.progress,0x10,maximum))return {};
        // 010f4bf0 clamps a nonzero percentage to [1,100]. 010f4b90 uses
        // the same native value/max ratio * 3500, truncated toward zero.
        if(!maximum || result.value>UINT32_MAX/100)return {};
        if(result.value)result.restored=std::clamp(result.value*100/maximum,1u,100u);
        const auto population=static_cast<long double>(result.value)/maximum*3500.0L;
        if(population>UINT32_MAX)return {};
        result.population=uint32_t(population);
    }
    if(!memory.Validate())return {};
    return result;
}
}
