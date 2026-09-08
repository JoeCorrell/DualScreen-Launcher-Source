#pragma once
#include "NativeRead.h"
#include <array>
#include <optional>
#include <cstring>

namespace ac2ds::native {
// Original build: b3e560 emits opcode14, 8-byte header, then six DWORDs.
// b3dd70 skips the header's uint16 inline-byte count before dispatching.
// Table 020dfe50[14] = b3ec10 forwards these DWORDs to D3D9 vtable+148.
// This decoder does not execute a command or retain any engine pointers.
constexpr uint32_t IndexedDrawCommandSlot=0x020dfe88;
constexpr uint32_t IndexedDrawCommandHandler=0x00b3ec10;
struct CharacterDrawCommand {
    uint32_t primitive{},minimumVertex{},vertexCount{},startIndex{},primitiveCount{};
    int32_t baseVertex{};
    uint32_t next{};
};
inline std::optional<CharacterDrawCommand> ReadCharacterDrawCommand(
        const Memory& memory,uint32_t payload,uint32_t end) {
    if(payload<0x10000 || end<payload || end-payload<24)return {};
    std::array<uint32_t,6> words{};
    if(!memory.Copy(payload,words.data(),sizeof(words)))return {};
    CharacterDrawCommand result;
    result.primitive=words[0];std::memcpy(&result.baseVertex,&words[1],4);
    result.minimumVertex=words[2];result.vertexCount=words[3];
    result.startIndex=words[4];result.primitiveCount=words[5];result.next=payload+24;
    // Character replay currently supports triangle lists/strips only. Do not
    // treat other valid game draw types as character geometry.
    if((result.primitive!=4 && result.primitive!=5) || !result.vertexCount || !result.primitiveCount ||
        uint64_t(result.minimumVertex)+result.vertexCount>UINT32_MAX ||
        uint64_t(result.startIndex)+(result.primitive==4?uint64_t(result.primitiveCount)*3:
            uint64_t(result.primitiveCount)+2)>UINT32_MAX)return {};
    return result;
}
inline std::optional<CharacterDrawCommand> ReadCharacterDrawPacket(
        const Memory& memory,uint32_t command,uint32_t end) {
    if(command<0x10000 || end<command || end-command<8)return {};
    std::array<uint32_t,2> header{};
    if(!memory.Copy(command,header.data(),sizeof(header)) || header[0]!=14)return {};
    uint32_t skip=header[1]&0xffffu;
    if(skip>end-command-8)return {};
    return ReadCharacterDrawCommand(memory,command+8+skip,end);
}
}
