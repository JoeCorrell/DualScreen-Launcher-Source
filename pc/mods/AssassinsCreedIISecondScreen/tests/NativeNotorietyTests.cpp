#include "InventoryFixture.h"
#include "NativeNotoriety.h"
#include <set>
#include <limits>
struct NotorietyFixture {
    OwnedFixture owned;MemoryFixture& m=owned.memory;
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    void Float(uint32_t a,float v){uint32_t bits;std::memcpy(&bits,&v,4);m.Word(a,bits);}
    NotorietyFixture(){
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Array(0x92060,0x98000,2);m.Word(0x98000,0x99000);m.Word(0x98004,0x9a000);
        m.Word(0x99000,0x1e7682c);m.Word(0x9a000,0x1e575c8);
        Float(0x9a00c,.25f);m.Word(0x223f648,0);Float(0x211c058,-1);
        std::set<uint32_t> pages;for(auto b:m.bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(p+i,0);
    }
};
struct ChangedRead final : Memory {
    const Memory& source;uint32_t target;mutable unsigned hits{};
    ChangedRead(const Memory& m,uint32_t at):source(m),target(at){}
    bool Copy(uint32_t a,void* out,size_t n)const override{
        if(!source.Copy(a,out,n))return false;
        if(a<=target && target-a<n && ++hits>1)static_cast<uint8_t*>(out)[target-a]^=1;
        return true;
    }
};
int main(){
    NotorietyFixture f;assert(ReadNotoriety(f.m,f.roots)==25.0);auto calls=f.m.reads;
    for(unsigned i=1;i<=calls;++i){NotorietyFixture x;x.m.failAt=i;assert(!ReadNotoriety(x.m,x.roots));}
    for(float v:{0.f,.5f,1.f}){NotorietyFixture x;x.Float(0x9a00c,v);assert(ReadNotoriety(x.m,x.roots)==double(v)*100);}
    for(float v:{-.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        NotorietyFixture x;x.Float(0x9a00c,v);assert(!ReadNotoriety(x.m,x.roots));
        x.m.Word(0x223f648,1);x.Float(0x211c058,v);assert(!ReadNotoriety(x.m,x.roots));
    }
    for(uint32_t enabled:{1u,255u}){
        NotorietyFixture x;x.m.Word(0x223f648,enabled);x.Float(0x211c058,.75f);
        x.Float(0x9a00c,std::numeric_limits<float>::quiet_NaN());assert(ReadNotoriety(x.m,x.roots)==75.0);
    }
    for(auto c:{std::pair<uint32_t,uint32_t>{0x9a000,0},{0x99000,0x1e575c8},{0x98004,0},
            {0x92064,0x1010101},{0x95000,0},{0x400f4,4},{0x223f814,0}}){
        NotorietyFixture x;x.m.Word(c.first,c.second);assert(!ReadNotoriety(x.m,x.roots));
    }
    for(uint32_t page:{0x98000u,0x9a000u,0x95000u}){
        NotorietyFixture x;x.m.mutateAddress=page;assert(!ReadNotoriety(x.m,x.roots));
    }
    for(uint32_t field:{0x223f648u,0x211c058u,0x9a00cu,0x2212490u}){
        NotorietyFixture x;
        if(field==0x211c058){x.m.Word(0x223f648,1);x.Float(field,.5f);}
        ChangedRead changed(x.m,field);assert(!ReadNotoriety(changed,x.roots));
    }
    std::puts("Notoriety: HUD override precedence, normalized range, current world/player, duplicate components and interrupted reads passed");
}
