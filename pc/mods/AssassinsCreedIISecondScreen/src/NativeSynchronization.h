#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
#include <array>

namespace ac2ds::native {
struct Synchronization {
    uint32_t earned{}, maximum{}, storyEarned{}, storyMaximum{};
    std::array<Collection,5> side{};
    Collection story{},sequences{};
    std::array<uint32_t,5> sideTitles{};
    double Percentage()const { return maximum ? 100.0*earned/maximum : 0.0; }
};
// Native StatsOverall: 010f85c0 -> 010f7130 / 010f7180. This is the
// game's weighted sum, including undiscovered memories in the denominator.
// No titles, objective locations, or locked content cross this interface.
inline std::optional<Synchronization> ReadSynchronization(const Memory& source,const Roots& roots){
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
    std::optional<uint32_t> entitlements;
    auto enabled=[&](uint32_t entry)->std::optional<bool>{
        uint32_t mask{};if(!memory.Member(entry,8,mask))return {};
        if(!mask)return true;
        // 0114c970: zero requirement OR ANY enabled DLC bit, not all bits.
        if(!entitlements){uint32_t config{},bits{};
            if(!memory.Read(0x22128b4,config) || !memory.Member(config,0x2c0,bits))return {};
            entitlements=bits;
        }
        return (mask & *entitlements)!=0;
    };
    auto contribution=[&](uint32_t entry,uint32_t expectedType)->std::optional<uint32_t>{
        uint32_t type{},handle{},state{},value{};
        if(!memory.Read(entry,type) || type!=expectedType || !memory.Member(entry,0x3c,handle) ||
            !memory.Reference(handle,state))return {};
        // 0126ea80 / 0159f970 return zero for a nonresident state handle.
        if(!state)return 0;
        if(!memory.Read(state,type) || type!=0x1cb8f58 || !memory.Member(state,0x1c,value) || value>100)return {};
        return value;
    };
    Synchronization result;PointerArray sequences{};
    if(!ArrayHeader(memory,catalog,8,32,sequences))return {};
    for(uint32_t i=0;i<sequences.Count();++i){
        uint32_t handle{},sequence{},type{};PointerArray entries{};
        if(!memory.Field(sequences.data,i*4,handle) || !memory.Reference(handle,sequence))return {};
        if(!sequence)continue;
        if(!memory.Read(sequence,type) || type!=0x1e76a60 || !ArrayHeader(memory,sequence,0x24,128,entries))return {};
        uint32_t sequenceCount{},sequenceComplete{};
        for(uint32_t j=0;j<entries.Count();++j){
            uint32_t reference{},entry{};
            if(!memory.Field(entries.data,j*4,reference) || !memory.Reference(reference,entry))return {};
            if(!entry)continue;
            auto allowed=enabled(entry);if(!allowed)return {};if(!*allowed)continue;
            auto value=contribution(entry,0x1e76a00);if(!value)return {};
            result.storyEarned+=*value;result.storyMaximum+=100;
            ++sequenceCount;++result.story.total;
            if(*value==100){++sequenceComplete;++result.story.completed;}
        }
        if(sequenceCount){++result.sequences.total;if(sequenceComplete==sequenceCount)++result.sequences.completed;}
    }
    result.earned=result.storyEarned;result.maximum=result.storyMaximum;
    for(uint32_t i=0;i<result.side.size();++i){
        uint32_t handle{},group{},type{};PointerArray entries{};
        if(!memory.Member(catalog,0x10+i*4,handle) || !memory.Reference(handle,group))return {};
        if(!group)continue;
        // 0129d4d0 serializes these five fields as CrowdMissionGroupInfo;
        // 015a0280 its +18 array as CrowdMissionSliceInfo.
        if(!memory.Read(group,type) || type!=0x1e9d0c0 || !ArrayHeader(memory,group,0x18,4096,entries))return {};
        // AdditionalGroupTitle 01597020 reads this shared text descriptor.
        if(!memory.Member(group,0xc,result.sideTitles[i]))return {};
        for(uint32_t j=0;j<entries.Count();++j){
            uint32_t reference{},entry{};
            if(!memory.Field(entries.data,j*4,reference) || !memory.Reference(reference,entry))return {};
            if(!entry)continue;
            auto allowed=enabled(entry);if(!allowed)return {};if(!*allowed)continue;
            auto value=contribution(entry,0x1e9d058);if(!value)return {};
            // Virtual +48 = 0159f610 returns 100 for this exact class.
            result.earned+=*value;result.maximum+=100;
            result.side[i].completedValue+=*value;++result.side[i].total;
            if(*value==100)++result.side[i].completed;
        }
    }
    if(!result.maximum || result.earned>result.maximum || !memory.Validate())return {};
    return result;
}
}
