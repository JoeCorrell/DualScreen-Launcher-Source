#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
#include <set>

namespace ac2ds::native {
// Exact-build, read-only renderer research. These are diagnostic identities,
// NOT retained engine objects: never dereference them after this transaction
// or send them to the UI/render thread. No Preview capability is enabled here.
// ae16b0/ae4ff0: visual+10 instance, instance+4 visual owner.
// b004f0/affee0: instance+2c skeleton, +30 uint32 bone remapping.
// b280d0/b2dd80: instance+8 is a ONE-BASED registration ID, not a pointer.
struct CharacterMeshSource {
    uint32_t component{}, instance{}, mesh{}, registration{}, renderer{};
    uint32_t materialOverride{};
    std::vector<uint32_t> bones;
};
struct CharacterSource {
    Roots roots{};
    uint32_t skeletonComponent{}, skeletonAsset{}, unsupportedInstances{};
    std::vector<CharacterMeshSource> meshes;
};
// Internal fields reader permits geometry traversal in the SAME transaction.
inline std::optional<CharacterSource> ReadCharacterSourceFields(const Memory& memory, const Roots& expected) {
    CharacterSource result;result.roots=expected;
    Roots current{};PointerArray components{};
    if(!expected.player || !expected.world || !ReadRoots(memory,current) || !(current==expected) ||
       !ArrayHeader(memory,expected.player,0x74,256,components))return {};
    std::set<uint32_t> identities;
    std::vector<uint32_t> visuals;
    for(uint32_t i=0;i<components.Count();++i){
        uint32_t component{},type{},owner{};
        if(!memory.Field(components.data,i*4,component) || !component ||
           !identities.insert(component).second || !memory.Read(component,type))return {};
        if(type!=0x01cb3568 && type!=0x01db4508)continue;
        if(!memory.Field(component,8,owner) || owner!=expected.player)return {};
        if(type==0x01cb3568){
            if(result.skeletonComponent || !memory.Field(component,0x1f0,result.skeletonAsset) ||
               result.skeletonAsset<0x10000)return {};
            result.skeletonComponent=component;
        }else visuals.push_back(component);
    }
    if(!result.skeletonComponent || visuals.size()>64)return {};
    for(auto component:visuals){
        CharacterMeshSource mesh;mesh.component=component;
        uint32_t type{},owner{},skeleton{},overrideHandle{};
        if(!memory.Field(component,0x10,mesh.instance))return {};
        if(!mesh.instance)continue; // Resource not instantiated yet.
        if(!memory.Read(mesh.instance,type))return {};
        if(type!=0x01db4e88){++result.unsupportedInstances;continue;}
        if(!memory.Field(mesh.instance,4,owner) || owner!=component ||
           !memory.Field(mesh.instance,0x10,mesh.mesh) || mesh.mesh<0x10000 ||
           !memory.Field(mesh.instance,0x2c,skeleton) || skeleton!=result.skeletonComponent ||
           !memory.Field(mesh.instance,0x38,overrideHandle) ||
           !memory.Reference(overrideHandle,mesh.materialOverride) ||
           !memory.Field(mesh.instance,8,mesh.registration))return {};
        PointerArray bones{};
        if(!ArrayHeader(memory,mesh.instance,0x30,256,bones))return {};
        mesh.bones.resize(bones.Count());
        for(uint32_t i=0;i<bones.Count();++i)
            if(!memory.Field(bones.data,i*4,mesh.bones[i]) || mesh.bones[i]>255)return {};
        if(mesh.registration){
            uint32_t sources{},sourceCount{},renderers{},rendererCount{},registered{};
            if(!memory.Read(0x0221da5c,sources) || !memory.Read(0x0221da60,sourceCount) ||
               !memory.Read(0x0221da68,renderers) || !memory.Read(0x0221da6c,rendererCount) ||
               sourceCount>262144 || rendererCount!=sourceCount || mesh.registration>sourceCount ||
               !memory.Field(sources,(mesh.registration-1)*4,registered) || registered!=mesh.instance ||
               !memory.Field(renderers,(mesh.registration-1)*4,mesh.renderer))return {};
            // A null renderer is a valid lazy slot; do not call b2dd80 to create it.
        }
        result.meshes.push_back(std::move(mesh));
    }
    if(result.meshes.empty())return {};
    return result;
}
inline std::optional<CharacterSource> ReadCharacterSource(const Memory& source,const Roots& expected){
    ReadTransaction memory(source);auto result=ReadCharacterSourceFields(memory,expected);
    if(!result || !memory.Validate())return {};return result;
}
}
