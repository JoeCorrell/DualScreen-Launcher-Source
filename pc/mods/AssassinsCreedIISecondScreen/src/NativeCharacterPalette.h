#pragma once
#include "NativeCharacterSource.h"
namespace ac2ds::native {
struct CharacterPaletteBone {
    uint8_t skeletonIndex{};
    bool hasBindRecord{};
    std::array<uint8_t,24> bindRecord{};
};
using CharacterPalette=std::vector<CharacterPaletteBone>;
// c062f0 builds a palette from mesh+24's 24-byte bind records and the
// MeshInstanceData+30 bone map. c051b0 stores 216-byte records: count at0,
// up to42 record pointers at4 and skeleton-index bytes at ac. c04430 uploads
// 3 vertex constants per bone starting at120 (42 bones fit the 256 registers).
// This reader copies bind bytes only; pose transforms are a separate source.
// Must be called inside the same ReadTransaction as ReadCharacterSourceFields.
inline bool ReadCharacterPaletteFields(const Memory& memory,const CharacterMeshSource& mesh,
                                       std::vector<CharacterPalette>& result){
    PointerArray bindings{},palettes{};
    if(!ArrayHeader(memory,mesh.mesh,0x24,256,bindings) || bindings.Count()!=mesh.bones.size() ||
       (bindings.Count() && bindings.data>UINT32_MAX-bindings.Count()*24) ||
       !ArrayHeader(memory,mesh.renderer,0x34,256,palettes) ||
       (palettes.Count() && palettes.data>UINT32_MAX-palettes.Count()*216))return false;
    result.resize(palettes.Count());
    for(uint32_t i=0;i<palettes.Count();++i){
        uint32_t count{},record=palettes.data+i*216;
        if(!memory.Read(record,count) || count>42)return false;
        auto& palette=result[i];palette.resize(count);
        for(uint32_t j=0;j<count;++j){
            uint32_t bind{};auto& bone=palette[j];
            if(!memory.Field(record,4+j*4,bind) || !memory.Member(record,0xac+j,bone.skeletonIndex))return false;
            if(!bind){if(bindings.Count() || bone.skeletonIndex)return false;continue;}
            if(bind<bindings.data || (bind-bindings.data)%24 || (bind-bindings.data)/24>=bindings.Count())return false;
            auto index=(bind-bindings.data)/24;
            if(mesh.bones[index]!=bone.skeletonIndex || !memory.Read(bind,bone.bindRecord))return false;
            bone.hasBindRecord=true;
        }
    }
    return true;
}
}
