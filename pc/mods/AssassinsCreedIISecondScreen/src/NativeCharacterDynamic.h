#pragma once
#include "NativeCharacterGeometry.h"

namespace ac2ds::native {
struct CharacterDynamicStream {
    uint32_t instance{},renderer{},geometry{},stride{},secondaryStride{},vertexCount{},primitiveCount{};
    uint32_t secondaryInterface{},indexInterface{};
    std::vector<uint8_t> vertices;
};
enum class CharacterDynamicStage { Graph, Fields, Bounds, Copy, Coherence, Complete };
// Diagnostic/CPU snapshot only. b36030 -> b089e0 uploads renderer+4c's
// renderer+58 bytes to a reused ring; b35600 uses geometry+1c as stream1.
// No game calls, AddRef, D3D calls or asynchronous publication of raw pointers.
// Only the verified full-detail index slot0 is supported; other LODs fail closed.
inline std::optional<std::vector<CharacterDynamicStream>> ReadCharacterDynamicStreams(
        const Memory& source,const Roots& roots,CharacterDynamicStage* diagnostic=nullptr){
    auto stage=[&](CharacterDynamicStage s){if(diagnostic)*diagnostic=s;};stage(CharacterDynamicStage::Graph);
    ReadTransaction memory(source);
    auto graph=ReadCharacterGeometry(memory,roots);
    if(!graph || graph->pending || graph->unsupported)return {};
    std::vector<CharacterDynamicStream> result;
    for(const auto& binding:graph->bindings){
        if(binding.kind!=CharacterRendererKind::Alternate)continue;
        stage(CharacterDynamicStage::Fields);
        CharacterDynamicStream item;item.instance=binding.instance;item.renderer=binding.renderer;item.geometry=binding.geometry;
        uint32_t data{},count{},capacity{},upload{},slot{},secondary{},indices{},selected{};
        if(!memory.Field(item.renderer,0x4c,data) || !memory.Field(item.renderer,0x50,count) ||
           !memory.Field(item.renderer,0x54,capacity) || !memory.Field(item.renderer,0x58,upload) ||
           !memory.Field(item.renderer,0x70,slot) || slot ||
           !memory.Field(item.geometry,4,item.primitiveCount) || !memory.Field(item.geometry,8,item.vertexCount) ||
           !memory.Field(item.geometry,0x10,item.stride) || !memory.Field(item.geometry,0x14,item.secondaryStride) ||
           !memory.Field(item.geometry,0x1c,secondary) || !memory.Field(item.geometry,0x20,indices) ||
           !memory.Field(item.renderer,0x60,selected) || selected!=indices ||
           !ReadCharacterBuffer(memory,secondary,0x01db5a6c,item.secondaryInterface) ||
           !ReadCharacterBuffer(memory,indices,0x01db5a78,item.indexInterface))return {};
        capacity&=0x3fffffff;
        stage(CharacterDynamicStage::Bounds);
        if(data<0x10000 || !upload || upload>65536 || upload>count || count>capacity || capacity>1048576 ||
           !item.vertexCount || item.vertexCount>65536 || !item.primitiveCount || item.primitiveCount>1048576 ||
           !item.stride || item.stride>256 || !item.secondaryStride || item.secondaryStride>256 ||
           uint64_t(item.vertexCount)*item.stride!=upload || !item.secondaryInterface || !item.indexInterface)return {};
        item.vertices.resize(upload);
        stage(CharacterDynamicStage::Copy);
        if(!memory.Copy(data,item.vertices.data(),upload))return {};
        result.push_back(std::move(item));
    }
    stage(CharacterDynamicStage::Coherence);if(!memory.Validate())return {};
    stage(CharacterDynamicStage::Complete);
    return result;
}
}
