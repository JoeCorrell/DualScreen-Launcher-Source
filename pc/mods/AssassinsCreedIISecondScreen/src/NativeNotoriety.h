#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
#include <cmath>

namespace ac2ds::native {
// Exact supported executable: HUD019e63e0 resolves the current world's component
// via0046cb50; getter010acfe0 selects the scripted override or component+c.
// Setter010addb0 clamps to [0,1]. No native call or game-state write is needed.
inline std::optional<double> ReadNotoriety(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots current{};
    if(!ReadRoots(memory,current) || !(current==roots) || !roots.world || !roots.player || !roots.progress ||
        !ResolvePlayerInventory(memory,roots.player))return {};
    PointerArray components{};
    if(!ArrayHeader(memory,roots.world,0x60,256,components))return {};
    uint32_t component{};
    for(uint32_t i=0;i<components.Count();++i){
        uint32_t pointer{},type{};
        if(!memory.Member(components.data,i*4,pointer) || !memory.Read(pointer,type))return {};
        if(type!=0x1e575c8)continue;
        if(component)return {}; // Never choose between duplicate instances.
        component=pointer;
    }
    uint8_t overridden{};float value{};
    if(!component || !memory.Read(0x223f648,overridden))return {};
    if(overridden){if(!memory.Read(0x211c058,value))return {};}
    else if(!memory.Member(component,0xc,value))return {};
    if(!std::isfinite(value) || value<0 || value>1 || !memory.Validate())return {};
    return double(value)*100.0;
}
}
