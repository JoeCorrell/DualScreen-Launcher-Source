#pragma once
#include "InventoryFixture.h"
#include "NativeCharacterSource.h"
#include <set>
struct CharacterFixture {
    MemoryFixture m;
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    CharacterFixture(){
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Array(0x40074,0x41000,2);m.Word(0x41000,0x42000);m.Word(0x41004,0x43000);
        m.Word(0x42000,0x1cb3568);m.Word(0x42008,roots.player);m.Word(0x421f0,0x50000);
        m.Word(0x43000,0x1db4508);m.Word(0x43008,roots.player);m.Word(0x43010,0x44000);
        m.Word(0x44000,0x1db4e88);m.Word(0x44004,0x43000);m.Word(0x44008,1);
        m.Word(0x44010,0x51000);m.Word(0x4402c,0x42000);m.Array(0x44030,0x45000,3);
        m.Word(0x45000,0);m.Word(0x45004,131);m.Word(0x45008,255);
        m.Word(0x44038,0x46000);m.Word(0x46000,0x47000);m.Word(0x46008,0x80000000);
        m.Word(0x221da5c,0x48000);m.Word(0x221da60,1);m.Word(0x48000,0x44000);
        m.Word(0x221da68,0x49000);m.Word(0x221da6c,1);m.Word(0x49000,0x4a000);
        std::set<uint32_t> pages;for(auto b:m.bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(p+i,0);
    }
};
struct ChangedCharacterRead final:Memory{
    const Memory& source;uint32_t target;mutable unsigned hits{};
    ChangedCharacterRead(const Memory& m,uint32_t a):source(m),target(a){}
    bool Copy(uint32_t a,void* out,size_t n)const override{
        if(!source.Copy(a,out,n))return false;
        if(a<=target && target-a<n && ++hits>1)static_cast<uint8_t*>(out)[target-a]^=1;
        return true;
    }
};
