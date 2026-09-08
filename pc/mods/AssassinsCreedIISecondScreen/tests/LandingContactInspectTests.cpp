#include "../tools/LandingContactInspect.h"
#include <map>
#include <cassert>
#include <cstdio>
using namespace ac2ds::landing_probe;
struct Fixture final:Memory {
    std::map<uint32_t,std::array<uint8_t,4096>> pages;
    mutable unsigned reads{};unsigned failAt{};uint32_t change{};
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(++reads==failAt)return false;
        for(size_t i=0;i<n;++i){auto p=pages.find((a+uint32_t(i))&~4095u);
            if(p==pages.end())return false;
            static_cast<uint8_t*>(out)[i]=p->second[(a+i)&4095];}
        if(change && reads>1 && a<=change && change-a<n)static_cast<uint8_t*>(out)[change-a]^=1;
        return true;
    }
    void Word(uint32_t a,uint32_t v){for(unsigned i=0;i<4;++i)pages[(a+i)&~4095u][(a+i)&4095]=uint8_t(v>>(i*8));}
    void Type(unsigned i,uint16_t first,uint16_t last){
        uint32_t c=0x60000+i*0x100,t=0x70000+i*0x100,g=0x500000+i*0x100,d=0x80000+i*0x100;
        Word(0x50000+i*4,c);Word(c,t);Word(t+0x10,g);
        Word(g,0xb8|(d<<8));Word(g+4,(d>>24)|0xc300);Word(d+0x54,first|(uint32_t(last)<<16));
    }
    Fixture(){
        Word(0x10034,0x20000);Word(0x20000,0x30000);Word(0x20008,0x80000000);
        Word(0x300e4,7);Word(0x10030,7);Word(0x1002c,0x40000);Word(0x40008,0x30000);
        Word(0x30074,0x50000);Word(0x30078,0x80030003);
        Word(0x20c91a8,0x20c91b0);Word(0x20c9204,10|(20u<<16));
        Word(0x209bc30,0x209bc38);Word(0x209bc8c,30|(40u<<16));
        Type(0,1,1);Type(1,25,25);Type(2,50,50);
    }
};
int main(){
    Fixture baseline;assert(Inspect(baseline,0x10000)==Filter::Accept);
    unsigned reads=baseline.reads;
    for(unsigned i=1;i<=reads;++i){Fixture f;f.failAt=i;assert(Inspect(f,0x10000)==Filter::Unknown);}
    {Fixture f;f.Type(1,11,12);assert(Inspect(f,0x10000)==Filter::Biped);}
    {Fixture f;f.Type(1,11,12);f.Word(0x500100,0x090000a1);f.Word(0x500104,0xc300);
        f.Word(0x90000,0x80100);assert(Inspect(f,0x10000)==Filter::Biped);} // A1 global getter
    {Fixture f;f.Type(1,31,32);assert(Inspect(f,0x10000)==Filter::Quadruped);}
    {Fixture f;f.Type(1,11,25);assert(Inspect(f,0x10000)==Filter::Accept);} // outside subtree
    {Fixture f;f.Type(1,11,12);f.Word(0x10030,8);auto before=f.pages;
        assert(Inspect(f,0x10000)==Filter::Accept);assert(f.pages==before);} // no cache write
    {Fixture f;f.Word(0x20008,0);assert(Inspect(f,0x10000)==Filter::Accept);}
    {Fixture f;f.Word(0x30078,0x81010101);assert(Inspect(f,0x10000)==Filter::Unknown);}
    {Fixture f;f.Word(0x500100,0xcccccccc);assert(Inspect(f,0x10000)==Filter::Unknown);}
    {Fixture f;f.Word(0x20c9204,0x7fff7fff);assert(Inspect(f,0x10000)==Filter::Unknown);}
    {Fixture f;f.change=0x10030;assert(Inspect(f,0x10000)==Filter::Unknown);}
    std::puts("Landing contact inspection: subtree exclusions, invalidation, failed/changing reads and no writes passed");
}
