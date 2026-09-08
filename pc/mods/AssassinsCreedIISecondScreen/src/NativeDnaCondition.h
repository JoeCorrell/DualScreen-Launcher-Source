#pragma once
#include "NativeRead.h"
namespace ac2ds::native {
// Read under the caller's transaction. 007e8ab0 applies +18 inversion to the
// exact virtual evaluator. The verified 01cd09b0 evaluator007e3070 checks an
// attached owner and the global mission selection. Resolving a nonzero global
// through00a11010 is still unsupported; no engine function is called here.
inline std::optional<bool> DnaCondition(const Memory& memory,uint32_t condition){
    uint32_t type{},owner{},selection{};uint8_t inverted{};
    if(!memory.Read(condition,type) || type!=0x1cd09b0 ||
        !memory.Member(condition,0x14,owner) || !memory.Member(condition,0x18,inverted))return {};
    bool result=false;
    if(owner){
        if(owner<0x10000 || !memory.Read(0x2214b64,selection) || selection)return {};
        result=true;
    }
    return inverted?!result:result;
}
// Mirrors proven005f7aa0 branches, including early condition rejection before
// the mode check. Unknown composite/polymorphic conditions remain unavailable.
inline std::optional<bool> DnaAvailability(const Memory& memory,uint32_t state,
        uint32_t configuration,uint8_t mode,uint32_t condition,uint8_t forced){
    if(!forced && condition){auto allowed=DnaCondition(memory,condition);if(!allowed || !*allowed)return allowed;}
    if(!mode || !condition)return true;
    uint32_t handle{},current{},type{};
    if(!memory.Member(configuration,0x934,handle) || !memory.Reference(handle,current))return {};
    if(current==state)return true;
    //005f4ea0 identifies this exact mission-selection condition and rejects it
    // in special mode when it is not the current mission state.
    if(!memory.Read(condition,type))return {};
    if(type==0x1cd09b0)return false;
    return {};
}
}
