#pragma once
#include "NativeMap.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace ac2ds::native;
struct Fixture : Memory {
    mutable std::map<uint32_t,uint8_t> bytes;
    mutable size_t calls{};
    size_t fail{};
    mutable uint32_t mutate{};
    Roots roots{0x10000,0x11000,0x12000,0x13000,0x14000};
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(++calls==fail)return false;
        for(size_t i=0;i<n;++i){auto p=bytes.find(a+uint32_t(i));if(p==bytes.end())return false;static_cast<uint8_t*>(out)[i]=p->second;}
        if(a==0x23000 && mutate){bytes[mutate]^=1;mutate=0;}
        return true;
    }
    void Zero(uint32_t a,size_t n){for(size_t i=0;i<n;++i)bytes[a+uint32_t(i)]=0;}
    void Word(uint32_t a,uint32_t v){for(uint32_t i=0;i<4;++i)bytes[a+i]=uint8_t(v>>(8*i));}
    void Float(uint32_t a,float f){uint32_t v;std::memcpy(&v,&f,4);Word(a,v);}
    void Ref(uint32_t a,uint32_t object,uint32_t id=0){Word(a,object);Word(a+4,0);Word(a+8,0x80000000);Word(a+12,id);}
    Fixture(){
        Word(0x2212490,roots.session);Word(0x2212570,roots.selection);Word(0x223f814,roots.progress);
        Word(0x110d0,UINT32_MAX);Word(0x1004c,roots.world);Word(0x1253c,0x15000);Word(0x15004,0x16000);
        Word(0x2214d00,0x17000);Word(0x17024,0);Word(0x16000,roots.player);
        Word(0x12060,0x18000);Word(0x12064,0x10001);Word(0x18000,0x19000);
        Word(0x19000,0x1e7682c);Word(0x19004,roots.world);Word(0x19008,0x1a000);bytes[0x19024]=0;Ref(0x1a000,0x1b000);
        Zero(0x1b000,136);Word(0x1b000,0x1e7579c);Word(0x1b00c,0x1c000);Ref(0x1c000,0,0x52a019fa);
        Float(0x1b034,1024);Float(0x1b038,1024);Float(0x1b03c,2);Float(0x1b040,-2);Float(0x1b044,512);Float(0x1b048,512);
        Word(0x1b06c,0x1d000);Word(0x1b074,123);Ref(0x1d000,0x1e000,456);
        Zero(0x1e000,88);Word(0x1e000,0x1db7ed0);Word(0x1e008,4);Word(0x1e00c,4);Word(0x1e010,1);Word(0x1e014,7);Word(0x1e054,0x23000);
        Zero(0x23000,16);bytes[0x23000]=255;bytes[0x2300f]=127;
        Zero(0x13010,68);Float(0x13040,64);Float(0x13044,128);
    }
};
