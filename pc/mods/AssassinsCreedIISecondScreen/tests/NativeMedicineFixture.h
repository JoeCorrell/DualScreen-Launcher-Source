#pragma once
#include "InventoryFixture.h"
#include <set>
struct MedicineFixture {
    OwnedFixture owned;MemoryFixture& m=owned.memory;
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    MedicineFixture(){
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Word(0x223f820,0x97000);m.Word(0x97000,0x1e60898);
        m.Word(0x4304c,0x98000);m.Word(0x98014,0x99000);m.Word(0x98038,6);
        constexpr uint32_t functions[]{0x15fde10,0x15fde30,0x15fde70,0x15f21b0,0x15f27c0,0x15f2880};
        for(uint32_t i=0;i<6;++i){
            m.Word(0x99004+i*24,((8000+i)<<3)|5);m.Word(0x99008+i*24,i<3?0x9a000:0x9c000);
            m.Word(0x99014+i*24,functions[i]);
        }
        m.Word(0x9a00c,0x9b000);m.Word(0x9b000,0x1ea1658);m.Word(0x9b020,0x43000);
        m.Word(0x9b058,6);m.Word(0x9b05c,10);m.Word(0x9b060,2);
        m.Word(0x223f730,8003);m.Word(0x223f708,8004);m.Word(0x223f728,8005);
        m.Word(0x9c000,0x1ea12e0);m.Word(0x9c00c,0x9d000);m.Word(0x9d000,0x1ea1310);
        m.Word(0x9d020,0x43000);m.Word(0x9d06c,1);
        m.Word(0x223f81c,0xa0000);m.Array(0xa000c,0xa1000,1);m.Word(0xa1000,0xa2000);
        m.Word(0xa2000,0x1e610c8);m.Array(0xa2008,0xa3000,1);m.Word(0xa3000,0xa4000);
        Reference(0xa4000,0xa5000);m.Word(0xa5010,3);m.Array(0xa5008,0xa6000,1);
        m.Word(0xa6000,0xa7000);Reference(0xa7000,0xa8000);
        m.Word(0xa8000,0x1e61020);m.Word(0xa8008,0xa9000);Reference(0xa9000,0xaa000);
        m.Word(0xa8014,14);m.Word(0xaa000,0x1e699bc);m.Word(0xaa00c,7);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
    void Reference(uint32_t address,uint32_t value){m.Word(address,value);m.Word(address+4,1);m.Word(address+8,0x80000000);}
};
