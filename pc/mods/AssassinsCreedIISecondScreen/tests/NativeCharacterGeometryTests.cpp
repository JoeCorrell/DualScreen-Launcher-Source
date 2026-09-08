#include "NativeCharacterFixture.h"
#include "NativeCharacterGeometry.h"
struct GeometryFixture:CharacterFixture{
    GeometryFixture(bool alternate=false){
        m.Word(0x51000,0x1db4cf0);m.Word(0x51038,1);
        m.Word(0x4a000,alternate?0x1db6848:0x1dcfbb4);
        m.Word(0x4a000+(alternate?0x20:0x28),0x56000);
        m.Word(0x221da1c,0x54000);m.Word(0x221da20,1);m.Word(0x54000,0x51000);
        m.Word(0x221da28,0x55000);m.Word(0x221da2c,1);m.Word(0x55000,0x56000);
        m.Word(0x56000,alternate?0x1db67a8:0x1dcf9f4);
        if(!alternate){
            m.Array(0x56008,0x57000,1);m.Array(0x56010,0x57000,1);
            m.Word(0x56020,32);m.Word(0x56024,0x58000);m.Word(0x56028,0x59000);
            m.Word(0x58000,0x1db5a6c);m.Word(0x58008,0x60000);
            m.Word(0x59000,0x1db5a78);m.Word(0x59008,0x61000);
            m.Word(0x57000,4);m.Word(0x57004,7);m.Word(0x57008,60);
            m.Word(0x5700c,90);m.Word(0x57010,20);
        }
        std::set<uint32_t> pages;for(auto b:m.bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(p+i,0);
    }
};
int main(){
    for(bool alternate:{false,true}){
        GeometryFixture f(alternate);auto g=ReadCharacterGeometry(f.m,f.roots);assert(g && g->bindings.size()==1);
        assert(g->bindings[0].geometry==0x56000 && g->bindings[0].registration==1 && !g->pending && !g->unsupported);
        assert(g->bindings[0].kind==(alternate?CharacterRendererKind::Alternate:CharacterRendererKind::Skinned));
        if(!alternate){
            const auto& b=g->bindings[0];assert(b.vertexStride==32 && b.vertexBuffer==0x58000 && b.indexBuffer==0x59000);
            assert(b.vertexInterface==0x60000 && b.indexInterface==0x61000);
            assert(b.primaryRanges.size()==1 && b.secondaryRanges.size()==1);
            const auto& r=b.primaryRanges[0];assert(r.primitive==4 && r.minVertex==7 && r.vertexCount==60 && r.startIndex==90 && r.primitiveCount==20);
        }
        auto reads=f.m.reads;
        for(unsigned i=1;i<=reads;++i){GeometryFixture x(alternate);x.m.failAt=i;assert(!ReadCharacterGeometry(x.m,x.roots));}
        for(uint32_t field:{0x43010u,0x4402cu,0x48000u,0x49000u,0x51038u,0x54000u,0x55000u,0x56000u,0x95000u}){
            GeometryFixture x(alternate);ChangedCharacterRead changed(x.m,field);assert(!ReadCharacterGeometry(changed,x.roots));
        }
        for(auto p:{std::pair<uint32_t,uint32_t>{0x51038,0},{0x51038,2},{0x221da20,262145},
                {0x221da2c,2},{0x54000,0x51004},{0x55000,0},{0x56000,0}}){
            GeometryFixture x(alternate);x.m.Word(p.first,p.second);assert(!ReadCharacterGeometry(x.m,x.roots));
        }
        GeometryFixture lazy(alternate);lazy.m.Word(0x55000,0);lazy.m.Word(0x4a000+(alternate?0x20:0x28),0);
        auto pending=ReadCharacterGeometry(lazy.m,lazy.roots);assert(pending && pending->pending==1 && pending->bindings.empty());
    }
    GeometryFixture unknown;unknown.m.Word(0x4a000,0x12345678);
    auto u=ReadCharacterGeometry(unknown.m,unknown.roots);assert(u && u->unsupported==1 && u->bindings.empty());
    // Unrelated changing bytes on a consumed page do not invalidate the graph.
    GeometryFixture f;ChangedCharacterRead unrelated(f.m,0x540f0);assert(ReadCharacterGeometry(unrelated,f.roots));
    for(auto p:{std::pair<uint32_t,uint32_t>{0x5600c,129u<<16},{0x56008,0xfffffff0},
        {0x57000,6},{0x57004,UINT32_MAX},{0x57008,UINT32_MAX},{0x5700c,4194304},{0x57010,UINT32_MAX},
        {0x58000,0x1db5a78},{0x59000,0x1db5a6c},{0x58008,1},{0x59008,1}}){
        GeometryFixture x;x.m.Word(p.first,p.second);assert(!ReadCharacterGeometry(x.m,x.roots));
    }
    for(uint32_t field:{0x56008u,0x56020u,0x56024u,0x56028u,0x57000u,0x57010u,0x58000u,0x58008u,0x59008u}){
        GeometryFixture x;ChangedCharacterRead changed(x.m,field);assert(!ReadCharacterGeometry(changed,x.roots));
    }
    GeometryFixture strip;strip.m.Word(0x57000,5);assert(ReadCharacterGeometry(strip.m,strip.roots));
    GeometryFixture lazyBuffers;lazyBuffers.m.Word(0x56024,0);lazyBuffers.m.Word(0x59008,0);
    auto lazy=ReadCharacterGeometry(lazyBuffers.m,lazyBuffers.roots);
    assert(lazy && !lazy->bindings[0].vertexInterface && !lazy->bindings[0].indexInterface);
    std::puts("Character geometry: both renderer paths, registry ownership, lazy slots and same-transaction lifetime guards passed");
}
