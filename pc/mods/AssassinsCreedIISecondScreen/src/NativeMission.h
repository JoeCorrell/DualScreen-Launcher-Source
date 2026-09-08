#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"

namespace ac2ds::native {
struct CurrentMission {
    bool present{}, side{};
    uint32_t group{}, index{}, title{}, description{}, sequenceTitle{}, synchronization{};
    std::optional<uint32_t> objective;
    uint64_t Identity() const {
        return (uint64_t((group+1) | (side ? 0x80000000u : 0u))<<32) | (index+1);
    }
};
// 01220990 -> 0129dda0 -> 0129da10 selects the FIRST state-5 main memory,
// then the first state-5 crowd memory in the five catalog groups. This is
// distinct from a selected DNA row and does not require opening a menu.
inline std::optional<CurrentMission> ReadCurrentMission(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    uint32_t manager{},catalog{};PointerArray catalogs{};
    if(!memory.Read(0x223f81c,manager) || !ArrayHeader(memory,manager,0xc,64,catalogs))return {};
    for(uint32_t i=0;i<catalogs.Count();++i){
        uint32_t candidate{},type{};
        if(!memory.Field(catalogs.data,i*4,candidate))return {};
        if(!candidate)continue;
        if(!memory.Read(candidate,type))return {};
        if(type==0x1e75650){if(catalog)return {};catalog=candidate;}
    }
    if(!catalog)return {};
    CurrentMission result;
    auto scan=[&](const PointerArray& entries,bool side,uint32_t group,uint32_t sequenceTitle)->bool{
        for(uint32_t i=0;i<entries.Count();++i){
            uint32_t reference{},entry{},type{},handle{},state{},phase{};
            if(!memory.Field(entries.data,i*4,reference) || !memory.Reference(reference,entry))return false;
            if(!entry)continue;
            if(!memory.Read(entry,type) || type!=(side?0x1e9d058u:0x1e76a00u) ||
                !memory.Member(entry,0x3c,handle) || !memory.Reference(handle,state))return false;
            if(!state)continue;
            if(!memory.Read(state,type) || type!=0x1cb8f58 || !memory.Member(state,0x18,phase) || phase>6)return false;
            if(phase!=5)continue;
            result.present=true;result.side=side;result.group=group;result.index=i;result.sequenceTitle=sequenceTitle;
            // PauseMissionTitle 01220e70 uses the shared +c text descriptor;
            // PauseMissionDescription 01220f2b accepts MainMissionSliceInfo only.
            if(!memory.Member(entry,0xc,result.title) ||
                (!side && !memory.Member(entry,0x54,result.description)) ||
                !memory.Member(state,0x1c,result.synchronization) || result.synchronization>100)return false;
            return true;
        }
        return true;
    };
    PointerArray sequences{};
    if(!ArrayHeader(memory,catalog,8,32,sequences))return {};
    for(uint32_t i=0;i<sequences.Count() && !result.present;++i){
        uint32_t handle{},sequence{},type{},title{};PointerArray entries{};
        if(!memory.Field(sequences.data,i*4,handle) || !memory.Reference(handle,sequence))return {};
        if(!sequence)continue;
        if(!memory.Read(sequence,type) || type!=0x1e76a60 || !memory.Member(sequence,0xc,title) ||
            !ArrayHeader(memory,sequence,0x24,128,entries) || !scan(entries,false,i,title))return {};
    }
    for(uint32_t i=0;i<5 && !result.present;++i){
        uint32_t handle{},group{},type{};PointerArray entries{};
        if(!memory.Member(catalog,0x10+i*4,handle) || !memory.Reference(handle,group))return {};
        if(!group)continue;
        if(!memory.Read(group,type) || type!=0x1e9d0c0 || !ArrayHeader(memory,group,0x18,4096,entries) ||
            !scan(entries,true,i,0))return {};
    }
    if(result.present){
        uint32_t ui{},type{},record{},text{};
        // PauseCurrentMissionObjective 01220f5e reads this native record.
        // Callback 0121f650 stores event-kind6 text+2c; 0121f6a0 resets it
        // to -1. The save service restores the same record via 01156f50.
        if(!memory.Read(0x223f820,ui) || !memory.Read(ui,type) || type!=0x1e60898 ||
            !memory.Member(ui,0x110,record))return {};
        if(record){if(!memory.Read(record,text))return {};if(text<0x80000000u)result.objective=text;}
    }
    if(!memory.Validate())return {};
    return result;
}
}
