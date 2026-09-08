#pragma once
#include "NativeEquipmentFixture.h"
#include "NativeCapes.h"
#include <set>
struct CapeFixture : EquipmentFixture {
    CapeFixture(){
        m.Word(0x12108,13);m.Word(0x22108,13);m.Word(0x5200c,13);m.Word(0x52010,0);
        m.Array(0x62008,0x63000,1);m.Word(0x65010,5);m.Array(0x65008,0x66000,2);m.Word(0x68014,22);
        m.Word(0x66004,0x67010);Ref(0x67010,0x68040);
        for(uint32_t i=0;i<12;++i)m.Word(0x68040+i*4,0);
        m.Word(0x68040,0x1e61020);m.Word(0x68048,0x51020);m.Word(0x68054,23);m.Word(0x68058,103);
        Ref(0x51020,0x52040,778);m.Word(0x52040,0x1e699bc);m.Word(0x5204c,13);m.Word(0x52050,1);m.Word(0x52070,0);
        m.Word(0x13004,0x50020);m.Word(0x50024,0x51020);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
};
