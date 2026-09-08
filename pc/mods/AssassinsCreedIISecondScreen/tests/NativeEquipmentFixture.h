#pragma once
#include "NativeEquipment.h"
#include "InventoryFixture.h"
struct EquipmentFixture {
    OwnedFixture owned;
    MemoryFixture& m=owned.memory;
    void Ref(uint32_t a,uint32_t object,uint32_t id=0){m.Word(a,object);m.Word(a+4,1);m.Word(a+8,0x80000000);m.Word(a+12,id);}
    EquipmentFixture(){
        m.Array(0x12118,0x13000,1);m.Word(0x13000,0x50000);m.Word(0x50004,0x51000);
        Ref(0x51000,0x52000,777);m.Word(0x52000,0x1e699bc);m.Word(0x5200c,2);m.Word(0x52030,0);
        m.Word(0x223f81c,0x60000);m.Array(0x6000c,0x61000,1);m.Word(0x61000,0x62000);
        m.Word(0x62000,0x1e610c8);m.Array(0x62008,0x63000,2);
        for(uint32_t i=0;i<2;++i){
            m.Word(0x63000+i*4,0x64000+i*16);Ref(0x64000+i*16,0x65000+i*32);
            m.Word(0x65010+i*32,i+1);m.Array(0x65008+i*32,0x66000,i?1:0);
        }
        m.Word(0x66000,0x67000);Ref(0x67000,0x68000);
        for(uint32_t i=0;i<12;++i)m.Word(0x68000+i*4,0);
        m.Word(0x68000,0x1e61020);m.Word(0x68008,0x51000);m.Word(0x68014,5);
        m.Word(0x68018,101);m.Word(0x68024,102);m.Word(0x6800c,0x69000);Ref(0x69000,0,888);
    }
};
