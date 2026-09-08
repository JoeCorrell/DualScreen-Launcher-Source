#pragma once
#include "NativeInventory.h"
#include "NativeReadTransaction.h"
namespace ac2ds::native {
// GET_GLYPH_PUZZLE_COUNT (017fcf2d) uses database+28 handle-array count.
// 017fa990 and GET_ANIMUS_DB_IS_MINIGAME_SOLVED use puzzle+44 bit1.
// Aggregate counts only: never expose undiscovered glyph locations or artwork.
inline std::optional<Collection> ReadGlyphs(const Memory& source,const Roots& roots){
    ReadTransaction memory(source);Roots before{};
    if(!ReadRoots(memory,before) || !(before==roots) || !roots.world || !roots.player || !roots.progress)return {};
    uint32_t database{},type{};PointerArray puzzles{};
    if(!memory.Read(0x22553b0,database) || !memory.Read(database,type) || type!=0x1ebb9b8 ||
        !ArrayHeader(memory,database,0x28,128,puzzles) || !puzzles.Count())return {};
    Collection result;result.total=puzzles.Count();
    for(uint32_t i=0;i<puzzles.Count();++i){
        uint32_t handle{},puzzle{};uint8_t state{};
        if(!memory.Field(puzzles.data,i*4,handle) || !memory.Reference(handle,puzzle) || !puzzle ||
            !memory.Read(puzzle,type) || type!=0x1ebb42c || !memory.Member(puzzle,0x44,state))return {};
        if(state&2)++result.completed;
    }
    if(!memory.Validate())return {};
    return result;
}
}
