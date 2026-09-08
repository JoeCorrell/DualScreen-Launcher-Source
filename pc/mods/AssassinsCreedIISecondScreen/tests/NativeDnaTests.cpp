#include "NativeDna.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
using namespace ac2ds::native;
struct Fixture : Memory {
    mutable std::map<uint32_t, uint8_t> bytes;
    mutable size_t calls{};
    size_t fail{};
    mutable uint32_t mutate{};
    bool Copy(uint32_t a, void* out, size_t n) const override {
        if (++calls == fail) return false;
        for (size_t i = 0; i < n; ++i) {
            auto it = bytes.find(a + uint32_t(i)); if (it == bytes.end()) return false;
            static_cast<uint8_t*>(out)[i] = it->second;
        }
        if (a == 0x80000 && mutate) { bytes[mutate] ^= 1; mutate = 0; }
        return true;
    }
    void Word(uint32_t a, uint32_t n) { for (uint32_t i = 0; i < 4; ++i) bytes[a + i] = uint8_t(n >> (i * 8)); }
    void Ref(uint32_t a, uint32_t p) { Word(a, p); Word(a + 4, 0); Word(a + 8, 0x80000000); }
    Fixture() {
        Word(0x223f81c, 0x10000); Word(0x1000c, 0x11000); Word(0x10010, 0x10001);
        Word(0x11000, 0x20000); Word(0x20000, 0x1e75650);
        Word(0x20008, 0x21000); Word(0x2000c, 0x10001); Word(0x21000, 0x22000); Ref(0x22000, 0x30000);
        for (uint32_t i = 0; i < 44; i += 4) Word(0x30000 + i, 0);
        Word(0x30000, 0x1e76a60); Word(0x3000c, 100); Word(0x30018, 0x31000); Ref(0x31000, 0x40000);
        Word(0x40000, 0x1cb8d38); Word(0x40020, 2);
        Word(0x30024, 0x50000); Word(0x30028, 0x10001); Word(0x50000, 0x60000); Ref(0x60000, 0x70000);
        for (uint32_t i = 0; i < 96; i += 4) Word(0x70000 + i, 0);
        Word(0x70000, 0x1e76a00); Word(0x7000c, 101); Word(0x70054, 102); Word(0x7003c, 0x71000); Ref(0x71000, 0x80000);
        Word(0x80000, 0x1cb8f58); Word(0x80018, 6); Word(0x8001c, 100); Word(0x80020, 0);
        bytes[0x80094] = 1; bytes[0x8009a] = 1;
        Word(0x22128bc, 0x90000); bytes[0x9005d] = 0;
        Word(0x22128b4,0x91000);Word(0x912c0,0);
        Word(0x80020,0);Word(0xa0000,0x1cd09b0);Word(0xa0014,0xb0000);bytes[0xa0018]=0;
        Word(0x2214b64,0);Word(0x90934,0xa1000);Ref(0xa1000,0x80000);
        std::set<uint32_t> pages;for(auto b:bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)bytes.emplace(p+i,0);
    }
};
struct ChangingCondition final : Memory {
    const Memory& source;uint32_t address;mutable unsigned reads{};
    ChangingCondition(const Memory& m,uint32_t at):source(m),address(at){}
    bool Copy(uint32_t a,void* out,size_t n)const override{
        if(!source.Copy(a,out,n))return false;
        if(a<=address && address-a<n && ++reads>1)static_cast<uint8_t*>(out)[address-a]^=1;
        return true;
    }
};
int main() {
    assert(DnaStatus(6, 0, 0, 0, 0, 0) == 3);
    assert(DnaStatus(5, 0, 0, 0, 0, 0) == 2);
    assert(DnaStatus(3, 0, 1, 0, 0, 0) == 2);
    assert(DnaStatus(3, 0, 0, 0, 0, 0) == 1);
    assert(!DnaStatus(1, 0, 1, 0, 0x12345, 0));
    assert(!DnaStatus(1, 0, 1, 1, 0, 1));
    assert(DnaStatus(1, 0, 1, 1, 0x12345, 0) == 2);
    assert(!DnaStatus(7, 0, 0, 0, 0, 0));
    Fixture f; auto dna = ReadDna(f); assert(dna && dna->size() == 1);
    assert((*dna)[0].title == 101 && (*dna)[0].description == 102 && (*dna)[0].state == 3);
    assert((*dna)[0].synchronization == 100 && !(*dna)[0].active);
    for (size_t i = 1; i <= f.calls; ++i) { Fixture broken; broken.fail = i; assert(!ReadDna(broken)); }
    for (uint32_t address : {0x223f81cu,0x11000u,0x20008u,0x21000u,0x22000u,0x3000cu,0x30024u,
            0x40020u,0x50000u,0x60000u,0x7000cu,0x71000u,0x80018u,0x8001cu,0x80020u,0x80094u,0x8009au,0x9005du}) {
        Fixture changed; changed.mutate = address; assert(!ReadDna(changed));
    }
    Fixture locked; locked.Word(0x40020, 1); assert(ReadDna(locked)->empty());
    Fixture dlc; dlc.Word(0x30008, 4); assert(ReadDna(dlc)->empty());
    dlc.Word(0x912c0,4);dna=ReadDna(dlc);assert(dna && dna->size()==1 && (*dna)[0].title==101);
    dlc.Word(0x30008,6);dna=ReadDna(dlc);assert(dna && dna->size()==1); // Native ANY-bit entitlement.
    for(uint32_t a:{0x22128b4u,0x912c0u}){Fixture changed;changed.Word(0x30008,4);changed.Word(0x912c0,4);changed.mutate=a;assert(!ReadDna(changed));}
    Fixture unknown; unknown.Word(0x70000, 0x1e76a01); assert(!ReadDna(unknown));
    Fixture unlocated; unlocated.Word(0x80018, 3); unlocated.bytes[0x80094] = 0;
    dna = ReadDna(unlocated); assert(dna && dna->size() == 1 && !(*dna)[0].title && !(*dna)[0].description);
    Fixture active; active.Word(0x80018, 5); active.Word(0x8001c, 0);
    dna = ReadDna(active); assert(dna && (*dna)[0].active);
    Fixture conditional;conditional.Word(0x80018,1);conditional.bytes[0x8009a]=0;conditional.Word(0x80020,0xa0000);
    dna=ReadDna(conditional);assert(dna && dna->size()==1 && (*dna)[0].title==101);
    conditional.bytes[0xa0018]=1;assert(ReadDna(conditional)->empty());
    conditional.Word(0xa0014,0);assert(ReadDna(conditional)->size()==1);
    conditional.bytes[0xa0018]=0;assert(ReadDna(conditional)->empty());
    conditional.Word(0xa0014,0xb0000);conditional.Word(0x2214b64,1);assert(ReadDna(conditional)->empty());
    conditional.Word(0x2214b64,0);conditional.bytes[0x9005d]=1;
    assert(ReadDna(conditional)->size()==1); // Exact current mission bypasses mode exclusion.
    conditional.Ref(0xa1000,0x81000);assert(ReadDna(conditional)->empty());
    conditional.Word(0x80020,0);assert(ReadDna(conditional)->size()==1); // No condition, even in special mode.
    Fixture checked;checked.Word(0x80018,1);checked.bytes[0x8009a]=0;checked.Word(0x80020,0xa0000);
    assert(ReadDna(checked)->size()==1);const auto conditionReads=checked.calls;
    for(size_t i=1;i<=conditionReads;++i){
        Fixture broken;broken.Word(0x80018,1);broken.bytes[0x8009a]=0;broken.Word(0x80020,0xa0000);broken.fail=i;
        assert(!ReadDna(broken));
    }
    for(uint32_t field:{0xa0000u,0xa0014u,0xa0018u,0x2214b64u}){
        ChangingCondition changed(checked,field);assert(!ReadDna(changed));
    }
    checked.bytes[0x9005d]=1;
    for(uint32_t field:{0x90934u,0xa1000u,0xa1008u}){
        ChangingCondition changed(checked,field);assert(!ReadDna(changed));
    }
    std::puts("DNA native status, discovery filtering, title hiding and interrupted-read tests passed");
}
