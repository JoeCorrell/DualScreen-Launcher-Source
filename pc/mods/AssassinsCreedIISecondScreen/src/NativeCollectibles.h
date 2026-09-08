#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
#include <set>

namespace ac2ds::native {
enum class CollectibleKind { Viewpoints, Treasure };
// AdditionalProgressCurrent/Max: 015ccd90/015ccdd0 -> slice015cc070/015cc0c0.
// Only aggregate counts leave this reader; district names/locations stay private.
inline std::optional<Collection> ReadAdditionalCollection(const Memory& source,const Roots& roots,CollectibleKind kind){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    if(kind!=CollectibleKind::Viewpoints && kind!=CollectibleKind::Treasure)return {};
    uint32_t manager{},catalog{};PointerArray catalogs{};
    if(!memory.Read(0x223f81c,manager) || !ArrayHeader(memory,manager,0xc,64,catalogs))return {};
    for(uint32_t i=0;i<catalogs.Count();++i){
        uint32_t p{},type{};if(!memory.Field(catalogs.data,i*4,p))return {};if(!p)continue;
        if(!memory.Read(p,type))return {};
        if(type==0x1e75650){if(catalog)return {};catalog=p;}
    }
    uint32_t handle{},group{},type{};PointerArray slices{};
    if(!catalog || !memory.Member(catalog,kind==CollectibleKind::Viewpoints?0x24:0x2c,handle) ||
        !memory.Reference(handle,group) || !group || !memory.Read(group,type) || type!=0x1e9efd0 ||
        !ArrayHeader(memory,group,0x18,64,slices))return {};
    std::set<uint32_t> collected;
    if(kind==CollectibleKind::Treasure){
        uint32_t owner{};PointerArray entries{};
        // 0126bac0 -> 00f7faa0 -> 00f7dea0 compares reference-handle identity
        // against the persistent collected inventory array, not visible markers.
        if(!memory.Read(0x223c590,owner) || !memory.Read(owner,type) || type!=0x1e3c920 ||
            !ArrayHeader(memory,owner,0x30,4096,entries))return {};
        for(uint32_t i=0;i<entries.Count();++i){uint32_t entry{};
            if(!memory.Field(entries.data,i*4,entry) || entry<0x10000)return {};
            collected.insert(entry);
        }
    }
    Collection result;uint32_t budget=8192;std::set<uint32_t> seenSlices,seenDistricts;
    for(uint32_t s=0;s<slices.Count();++s){
        uint32_t slice{};PointerArray districts{};
        if(!memory.Field(slices.data,s*4,handle) || !memory.Reference(handle,slice))return {};
        if(!slice)continue;
        if(!seenSlices.insert(slice).second || !memory.Read(slice,type) || type!=0x1e9ef50 ||
            !ArrayHeader(memory,slice,0x3c,64,districts))return {};
        for(uint32_t d=0;d<districts.Count();++d){
            uint32_t district{};PointerArray points{};
            if(!memory.Field(districts.data,d*4,handle) || !memory.Reference(handle,district))return {};
            if(!district)continue;
            if(!seenDistricts.insert(district).second || !memory.Read(district,type))return {};
            if(kind==CollectibleKind::Viewpoints){
                if(type!=0x1e692d8 || !ArrayHeader(memory,district,0x18,4096,points))return {};
            }else{
                uint32_t collection{};
                if(type!=0x1e99c78 || !memory.Member(district,0x18,handle) || !memory.Reference(handle,collection))return {};
                if(!collection)continue; // Native15642f0/1564310 both return0.
                if(!memory.Read(collection,type) || type!=0x1e71ef8 || !ArrayHeader(memory,collection,0xe0,255,points))return {};
                // Original max and accumulated current are bytes. Reject an
                // unsupported oversized district instead of emulating wraparound.
            }
            if(points.Count()>budget)return {};budget-=points.Count();result.total+=points.Count();
            for(uint32_t p=0;p<points.Count();++p){
                uint32_t point{};
                if(!memory.Field(points.data,p*4,handle) || !memory.Reference(handle,point))return {};
                if(!point)continue;
                if(!memory.Read(point,type))return {};
                if(kind==CollectibleKind::Viewpoints){
                    uint32_t state{};
                    // RHPDistrictInfo011f04f0 counts MissionStepScene states3/5.
                    if(type!=0x1ccf940 || !memory.Member(point,0x1c,state) || state>5)return {};
                    if(state==3 || state==5)++result.completed;
                }else{
                    uint32_t category{};
                    if(type!=0x1e699bc || !memory.Member(point,0xc,category) || category!=9)return {};
                    if(collected.count(handle))++result.completed;
                }
            }
        }
    }
    if(!memory.Validate())return {};
    return result;
}
}
