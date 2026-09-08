#pragma once
#include "NativeSynchronization.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace ac2ds::native;
struct Fixture : Memory {
    mutable std::map<uint32_t,std::array<uint8_t,4096>> pages;
    mutable size_t calls{};size_t fail{};
    mutable uint32_t mutate{};
    Roots roots{0x10000,0x11000,0x12000,0x13000,0x14000};
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(++calls==fail)return false;
        for(size_t i=0;i<n;++i){auto p=pages.find((a+uint32_t(i))&~4095u);if(p==pages.end())return false;
            static_cast<uint8_t*>(out)[i]=p->second[(a+i)&4095];}
        if(a==0x80000 && mutate){pages[mutate&~4095u][mutate&4095]^=1;mutate=0;}
        return true;
    }
    void Word(uint32_t a,uint32_t v){for(uint32_t i=0;i<4;++i)pages[(a+i)&~4095u][(a+i)&4095]=uint8_t(v>>(i*8));}
    void Ref(uint32_t a,uint32_t p){Word(a,p);Word(a+8,0x80000000);}
    Fixture(){
        Word(0x2212490,roots.session);Word(0x2212570,roots.selection);Word(0x223f814,roots.progress);
        Word(0x110d0,UINT32_MAX);Word(0x1004c,roots.world);Word(0x1253c,0x15000);Word(0x15004,0x16000);
        Word(0x2214d00,0x17000);Word(0x17024,0);Word(0x16000,roots.player);
        Word(0x223f81c,0x18000);Word(0x1800c,0x19000);Word(0x18010,0x10001);Word(0x19000,0x20000);
        Word(0x20000,0x1e75650);Word(0x20008,0x21000);Word(0x2000c,0x10001);Word(0x21000,0x22000);Ref(0x22000,0x30000);
        Word(0x30000,0x1e76a60);Word(0x30024,0x31000);Word(0x30028,0x20002);
        for(uint32_t i=0;i<2;++i){Word(0x31000+i*4,0x32000+i*16);Ref(0x32000+i*16,0x40000+i*0x100);
            Word(0x40000+i*0x100,0x1e76a00);Word(0x4003c+i*0x100,0x42000+i*16);Ref(0x42000+i*16,0x50000+i*0x100);
            Word(0x50000+i*0x100,0x1cb8f58);Word(0x5001c+i*0x100,i?50:100);}
        for(uint32_t i=0;i<5;++i){Word(0x20010+i*4,0x23000+i*16);Ref(0x23000+i*16,i?0:0x60000);}
        Word(0x60000,0x1e9d0c0);Word(0x60018,0x61000);Word(0x6001c,0x10001);
        Word(0x61000,0x62000);Ref(0x62000,0x70000);Word(0x70000,0x1e9d058);Word(0x7003c,0x72000);
        Ref(0x72000,0x80000);Word(0x80000,0x1cb8f58);Word(0x8001c,25);
        Word(0x22128b4,0x90000);Word(0x902c0,4);
    }
};
