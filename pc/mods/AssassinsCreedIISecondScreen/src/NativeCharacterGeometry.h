#pragma once
#include "NativeCharacterSource.h"
namespace ac2ds::native {
enum class CharacterRendererKind { Skinned, Alternate };
enum class CharacterGeometryStage { Source, Ownership, Buffers, Ranges, Coherence, Complete };
// bfdee0 submits these to IDirect3DDevice9::DrawIndexedPrimitive with
// BaseVertexIndex=0. These are copied values, not CPU vertex/index data.
struct CharacterDrawRange {
    uint32_t primitive{},minVertex{},vertexCount{},startIndex{},primitiveCount{};
};
struct CharacterGeometryBinding {
    uint32_t instance{},mesh{},renderer{},geometry{},registration{};
    CharacterRendererKind kind{};
    uint32_t vertexStride{},vertexBuffer{},indexBuffer{};
    uint32_t vertexInterface{},indexInterface{}; // Diagnostic identities; no AddRef/COM calls.
    std::vector<CharacterDrawRange> primaryRanges,secondaryRanges;
};
struct CharacterGeometry {
    CharacterSource character;
    std::vector<CharacterGeometryBinding> bindings;
    uint32_t pending{},unsupported{};
};
inline bool ReadCharacterDrawRanges(const Memory& memory,uint32_t geometry,uint32_t offset,
                                    std::vector<CharacterDrawRange>& result){
    PointerArray ranges{};
    if(!ArrayHeader(memory,geometry,offset,128,ranges) ||
       (ranges.Count() && ranges.data>UINT32_MAX-ranges.Count()*20))return false;
    result.resize(ranges.Count());
    for(uint32_t i=0;i<ranges.Count();++i){
        auto& r=result[i];
        if(!memory.Read(ranges.data+i*20,r) || (r.primitive!=4 && r.primitive!=5) ||
           r.vertexCount>1048576 || r.minVertex>1048576 || r.primitiveCount>1048576 ||
           r.startIndex>4194304 || uint64_t(r.minVertex)+r.vertexCount>1048576 ||
           uint64_t(r.startIndex)+(r.primitive==4?uint64_t(r.primitiveCount)*3:
                                  (r.primitiveCount?uint64_t(r.primitiveCount)+2:0))>4194304)return false;
    }
    return true;
}
static_assert(sizeof(CharacterDrawRange)==20,"Native draw range layout");
// b11970/b118d0: 12-byte wrappers, verified type at +0 and COM identity at +8.
// A null lazy wrapper/interface is valid. Never inspect DXVK's private layout.
inline bool ReadCharacterBuffer(const Memory& memory,uint32_t wrapper,uint32_t expected,uint32_t& object){
    object=0;if(!wrapper)return true;
    uint32_t type{};
    return wrapper>=0x10000 && memory.Read(wrapper,type) && type==expected &&
        memory.Field(wrapper,8,object) && (!object || object>=0x10000);
}
// Diagnostic graph only, not a renderable/retained scene. Separate mesh-resource
// registry: b280c0 -> sources221da1c/count20, render objects221da28/count2c.
// af9a20 returns mesh+38's ONE-BASED ID. c04240/b370b0 bind the object to
// skinned renderer+28 / alternate renderer+20 respectively. Never invoke these
// constructors/getters: they allocate GPU objects and consume upload buffers.
inline std::optional<CharacterGeometry> ReadCharacterGeometry(const Memory& source,const Roots& roots,
        CharacterGeometryStage* diagnostic=nullptr){
    auto stage=[&](CharacterGeometryStage value){if(diagnostic)*diagnostic=value;};
    stage(CharacterGeometryStage::Source);
    ReadTransaction memory(source);auto character=ReadCharacterSourceFields(memory,roots);
    if(!character)return {};
    CharacterGeometry result;result.character=std::move(*character);
    for(const auto& m:result.character.meshes){
        stage(CharacterGeometryStage::Ownership);
        if(!m.renderer){++result.pending;continue;}
        uint32_t rendererType{},meshType{},offset{},expectedType{};
        if(!memory.Read(m.renderer,rendererType) || !memory.Read(m.mesh,meshType))return {};
        CharacterGeometryBinding binding;binding.instance=m.instance;binding.mesh=m.mesh;binding.renderer=m.renderer;
        if(rendererType==0x01dcfbb4){offset=0x28;expectedType=0x01dcf9f4;binding.kind=CharacterRendererKind::Skinned;}
        else if(rendererType==0x01db6848){offset=0x20;expectedType=0x01db67a8;binding.kind=CharacterRendererKind::Alternate;}
        else{++result.unsupported;continue;}
        if(meshType!=0x01db4cf0){++result.unsupported;continue;}
        uint32_t sources{},sourceCount{},objects{},objectCount{},registered{},linked{},geometryType{};
        if(!memory.Field(m.mesh,0x38,binding.registration) || !memory.Field(m.renderer,offset,linked))return {};
        if(!binding.registration){if(linked)return {};++result.pending;continue;}
        if(!memory.Read(0x0221da1c,sources) || !memory.Read(0x0221da20,sourceCount) ||
           !memory.Read(0x0221da28,objects) || !memory.Read(0x0221da2c,objectCount) ||
           sourceCount>262144 || sourceCount!=objectCount || binding.registration>sourceCount ||
           !memory.Field(sources,(binding.registration-1)*4,registered) || registered!=m.mesh ||
           !memory.Field(objects,(binding.registration-1)*4,binding.geometry) || binding.geometry!=linked)return {};
        if(!binding.geometry){++result.pending;continue;}
        if(!memory.Read(binding.geometry,geometryType) || geometryType!=expectedType)return {};
        if(binding.kind==CharacterRendererKind::Skinned){
            stage(CharacterGeometryStage::Buffers);
            uint32_t flags{};
            if(!memory.Field(binding.geometry,0x20,flags) ||
               !memory.Field(binding.geometry,0x24,binding.vertexBuffer) ||
               !memory.Field(binding.geometry,0x28,binding.indexBuffer) ||
               !ReadCharacterBuffer(memory,binding.vertexBuffer,0x01db5a6c,binding.vertexInterface) ||
               !ReadCharacterBuffer(memory,binding.indexBuffer,0x01db5a78,binding.indexInterface))return {};
            stage(CharacterGeometryStage::Ranges);
            if(!ReadCharacterDrawRanges(memory,binding.geometry,8,binding.primaryRanges) ||
               !ReadCharacterDrawRanges(memory,binding.geometry,0x10,binding.secondaryRanges))return {};
            binding.vertexStride=flags&0x7ff;
        }
        // Alternate renderers use dynamic per-instance streams (b36030).
        // Their shared geometry buffers are not a complete drawable mesh.
        result.bindings.push_back(std::move(binding));
    }
    stage(CharacterGeometryStage::Coherence);
    if(!memory.Validate())return {};stage(CharacterGeometryStage::Complete);return result;
}
}
