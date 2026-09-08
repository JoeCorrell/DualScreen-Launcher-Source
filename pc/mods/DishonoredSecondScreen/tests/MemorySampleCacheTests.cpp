#include "../src/GameMemory.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace dishonored;
struct Fixture { unsigned calls{};uint32_t value{42};bool fail{}; };
bool Read(Address address,void* data,size_t size,void* context) noexcept {
    auto& f=*static_cast<Fixture*>(context);++f.calls;
    if(f.fail || address<0x10000 || size>4)return false;
    std::memcpy(data,&f.value,size);return true;
}
int main() {
    Fixture fixture;Memory memory(0x400000,Read,&fixture);uint32_t value{};
    memory.Begin(65536,4,true);
    for(unsigned i=0;i<1000;++i){assert(memory.Read(0x10000,value));assert(value==42);}
    assert(fixture.calls==1 && memory.CallsUsed()==1 && memory.CacheHits()==999);
    // Beginning another sample discards every cached pointer/value immediately.
    fixture.value=84;memory.Begin(1024,4,true);assert(memory.Read(0x10000,value) && value==84);
    // Command/post-sample phases are uncached by default, including mutations
    // between native actions. Never return a pre-action selection as current.
    memory.Begin();assert(memory.Read(0x10000,value) && value==84);
    fixture.value=99;assert(memory.Read(0x10000,value) && value==99);
    assert(memory.CacheHits()==0);
    // Failed reads never poison a later valid read at the same address.
    memory.Begin(1024,4,true);fixture.fail=true;assert(!memory.Read(0x10000,value));
    fixture.fail=false;assert(memory.Read(0x10000,value) && value==99);
    // Size is part of the key. A cached byte is not a valid DWORD.
    memory.Begin(1024,4,true);uint8_t byte{};assert(memory.Read(0x10000,byte));
    assert(memory.Read(0x10000,value));assert(memory.CallsUsed()==2);
    // Caching does not bypass byte/work limits or invalid spans.
    memory.Begin(4,1,true);assert(memory.Read(0x10000,value));assert(!memory.Read(0x10000,value));
    assert(memory.LastFailure().reason==Memory::Failure::Budget);
    memory.Begin(1024,1,true);assert(!memory.Read(0,value));assert(memory.CallsUsed()==0);
    puts("PASS: repeated read reduction, sample invalidation, uncached native mutations, failed reads, size identity and budgets");
}
