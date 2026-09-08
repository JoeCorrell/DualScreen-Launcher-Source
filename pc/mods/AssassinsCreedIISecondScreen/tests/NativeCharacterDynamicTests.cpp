#include "NativeCharacterFixture.h"
#include "NativeCharacterDynamic.h"
struct DynamicFixture:CharacterFixture {
    DynamicFixture(){
        m.Word(0x51000,0x1db4cf0);m.Word(0x51038,1);
        m.Word(0x4a000,0x1db6848);m.Word(0x4a020,0x56000);
        m.Word(0x221da1c,0x54000);m.Word(0x221da20,1);m.Word(0x54000,0x51000);
        m.Word(0x221da28,0x55000);m.Word(0x221da2c,1);m.Word(0x55000,0x56000);
        m.Word(0x56000,0x1db67a8);m.Word(0x56004,1);m.Word(0x56008,3);
        m.Word(0x56010,24);m.Word(0x56014,12);m.Word(0x5601c,0x58000);m.Word(0x56020,0x59000);
        m.Word(0x58000,0x1db5a6c);m.Word(0x58008,0x60000);
        m.Word(0x59000,0x1db5a78);m.Word(0x59008,0x61000);
        m.Word(0x4a04c,0x62000);m.Word(0x4a050,72);m.Word(0x4a054,0xc0000048);
        m.Word(0x4a058,72);m.Word(0x4a060,0x59000);m.Word(0x4a070,0);
        for(unsigned i=0;i<72;++i)m.bytes[0x62000+i]=uint8_t(i);
        std::set<uint32_t> pages;for(auto b:m.bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(p+i,0);
    }
};
int main(){
    DynamicFixture f;auto streams=ReadCharacterDynamicStreams(f.m,f.roots);
    assert(streams && streams->size()==1);auto& s=streams->front();
    assert(s.stride==24 && s.secondaryStride==12 && s.vertexCount==3 && s.primitiveCount==1);
    assert(s.secondaryInterface==0x60000 && s.indexInterface==0x61000 && s.vertices.size()==72);
    for(unsigned i=0;i<72;++i)assert(s.vertices[i]==i);
    auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){DynamicFixture x;x.m.failAt=i;assert(!ReadCharacterDynamicStreams(x.m,x.roots));}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x4a058,65537},{0x4a050,71},{0x4a054,71},
            {0x4a04c,0xfffffff0},{0x4a070,1},{0x4a060,0x58000},{0x56010,0},{0x56008,4},
            {0x56014,0},{0x58000,0x1db5a78},{0x59008,0},{0x4a020,0}}){
        DynamicFixture x;x.m.Word(change.first,change.second);assert(!ReadCharacterDynamicStreams(x.m,x.roots));
    }
    for(auto address:{0x4a04cu,0x4a058u,0x4a070u,0x58008u,0x59008u,0x62004u,0x95000u}){
        DynamicFixture x;ChangedCharacterRead changed(x.m,address);assert(!ReadCharacterDynamicStreams(changed,x.roots));
    }
    std::puts("Dynamic character stream: bounded owned bytes, index slot, stream types, ownership and mutation rejection passed");
}
