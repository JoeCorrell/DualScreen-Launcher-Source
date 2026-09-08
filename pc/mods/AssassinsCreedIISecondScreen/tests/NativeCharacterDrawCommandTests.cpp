#include "NativeCharacterDrawCommand.h"
#include <cassert>
#include <iostream>
using namespace ac2ds::native;
struct Bytes final:Memory {
    std::array<uint32_t,12> words{};
    bool Copy(uint32_t address,void* out,size_t size)const override {
        if(address<0x10000 || uint64_t(address-0x10000)+size>sizeof(words))return false;
        std::memcpy(out,reinterpret_cast<const uint8_t*>(words.data())+address-0x10000,size);return true;
    }
};
int main(){
    Bytes memory;memory.words={14,0,4,0xfffffffe,2,205,10,357};
    auto draw=ReadCharacterDrawPacket(memory,0x10000,0x10020);
    assert(draw && draw->baseVertex==-2 && draw->vertexCount==205 && draw->primitiveCount==357 && draw->next==0x10020);
    assert(!ReadCharacterDrawPacket(memory,0x10000,0x1001f));
    assert(!ReadCharacterDrawPacket(memory,0xfffffff8,0x10020));
    memory.words[0]=13;assert(!ReadCharacterDrawPacket(memory,0x10000,0x10020));
    memory.words={14,4,0,5,0,0,510,0,595};
    draw=ReadCharacterDrawPacket(memory,0x10000,0x10024);
    assert(draw && draw->primitive==5 && draw->next==0x10024);
    memory.words[1]=0xffff;assert(!ReadCharacterDrawPacket(memory,0x10000,0x10024));
    memory.words={14,0,4,0,0,205,0xffffffff,357};
    assert(!ReadCharacterDrawPacket(memory,0x10000,0x10020));
    memory.words={14,0,4,0,0xffffffff,205,0,357};
    assert(!ReadCharacterDrawPacket(memory,0x10000,0x10020));
    std::cout << "Native queued draw: payload order, signed base vertex, inline bytes, truncation and overflow passed\n";
}
