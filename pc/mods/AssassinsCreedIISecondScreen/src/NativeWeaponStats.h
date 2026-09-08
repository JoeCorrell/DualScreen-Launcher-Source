#pragma once
#include "NativeEquipment.h"
#include "NativeReadTransaction.h"

namespace ac2ds::native {
struct WeaponStats { unsigned damage{},speed{},deflection{}; };

// The resident branch of 009d00a0/009d01a0. 009d1520 registers the
// resource's pool index at object+4. Verify the reverse mapping instead of
// invoking 009d18d0, which may allocate/load resources and acquire engine locks.
inline bool CanonicalResidentReference(const Memory& memory,uint32_t handle,uint32_t object) {
    uint32_t encoded{},pages{};uint16_t count{};
    if(!memory.Field(object,4,encoded) || !memory.Read(0x2214ec4,pages) ||
        !memory.Read(0x2214eca,count))return false;
    const uint32_t index=(encoded&0xc3ffffffu)-1, page=index>>14, slot=index&0x3fff;
    count&=0x3fff;
    if(index==UINT32_MAX || count>256 || page>=count || pages<0x10000 ||
        pages>UINT32_MAX-page*12-8)return false;
    uint32_t base{},capacity{};
    if(!memory.Read(pages+page*12,base) || !memory.Read(pages+page*12+4,capacity) ||
        !capacity || capacity>0x4000 || slot>=capacity || base<0x10000 ||
        base>UINT32_MAX-slot*16)return false;
    return base+slot*16==handle;
}

// Original inventory popup 012d9f70 -> 010dd220 -> 00f81840 -> 0054e580.
// Called on the state sampling path, never the render path. No engine calls,
// handles retained, inventory mutations or constructor-default substitutions.
inline std::optional<WeaponStats> ReadWeaponStats(const Memory& source,const Roots& roots,
        const EquipmentRecord& requested) {
    if(requested.menu<4 || requested.menu>8 || !requested.definitionHandle)return {};
    ReadTransaction memory(source);Roots current{};
    if(!ReadRoots(memory,current) || !(current==roots) || !roots.player)return {};
    auto equipment=ReadEquipment(memory,roots.player);if(!equipment)return {};
    bool owned=false;
    for(const auto& item:*equipment)if(item.id==requested.id && item.menu==requested.menu &&
        item.definitionHandle==requested.definitionHandle){if(owned)return {};owned=true;}
    if(!owned)return {};
    uint32_t definition{};
    if(!memory.Reference(requested.definitionHandle,definition) || !definition)return {};
    WeaponStats result;
    // Explicit native popup branches, only after validating live ownership/ID.
    if(requested.id==0x0bcd04a6)result={1,5,1};
    else if(requested.id==0x26740bd7)result={3,5,1};
    else {
        uint32_t manager{},type{};PointerArray pairs{};
        if(!memory.Read(0x223c590,manager) || !memory.Read(manager,type) || type!=0x1e3c920 ||
            !ArrayHeader(memory,manager,0x178,4096,pairs) ||
            (pairs.Count() && pairs.data>UINT32_MAX-pairs.Count()*8))return {};
        uint32_t entityHandle{};
        for(uint32_t i=0;i<pairs.Count();++i){
            uint32_t candidateHandle{},candidate{};
            if(!memory.Read(pairs.data+i*8,candidateHandle) ||
                !memory.Reference(candidateHandle,candidate))return {};
            if(candidate==definition){
                // The native loop replaces its output for every match: last wins.
                if(!memory.Read(pairs.data+i*8+4,entityHandle))return {};
            }
        }
        uint32_t entity{};
        if(!entityHandle || !memory.Reference(entityHandle,entity) || !entity ||
            !CanonicalResidentReference(memory,entityHandle,entity))return {};
        PointerArray components{};uint32_t weapon{};
        if(!ArrayHeader(memory,entity,0x74,256,components))return {};
        for(uint32_t i=0;i<components.Count();++i){
            uint32_t component{},componentType{};
            if(!memory.Read(components.data+i*4,component) ||
                (component && !memory.Read(component,componentType)))return {};
            // Exact WeaponComponent constructor 010faf90, not a guessed subtype.
            if(componentType==0x1e5b2d0){if(weapon)return {};weapon=component;}
        }
        std::array<uint8_t,3> values{};
        if(!weapon || !memory.Member(weapon,0x20,values))return {};
        result={values[2],values[0],values[1]};
        if(result.damage>5 || result.speed>5 || result.deflection>5)return {};
    }
    if(!memory.Validate())return {};
    return result;
}
}
