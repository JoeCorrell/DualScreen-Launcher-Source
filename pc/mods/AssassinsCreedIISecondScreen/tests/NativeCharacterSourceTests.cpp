#include "NativeCharacterFixture.h"
int main(){
    CharacterFixture f;auto value=ReadCharacterSource(f.m,f.roots);assert(value);
    assert(value->skeletonComponent==0x42000 && value->meshes.size()==1);
    const auto& mesh=value->meshes[0];assert(mesh.renderer==0x4a000 && mesh.materialOverride==0x47000);
    assert((mesh.bones==std::vector<uint32_t>{0,131,255}));auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){CharacterFixture x;x.m.failAt=i;assert(!ReadCharacterSource(x.m,x.roots));}
    for(auto p:{std::pair<uint32_t,uint32_t>{0x95000,0},{0x42008,0x12345},{0x43008,0},
            {0x44004,0x43004},{0x4402c,0},{0x421f0,0},{0x44010,0},{0x44008,2},
            {0x221da60,262145},{0x221da6c,2},{0x48000,0x44004},{0x45004,256},
            {0x44034,0x01010101},{0x41004,0x42000},{0x44000,0x12345678}}){
        CharacterFixture x;x.m.Word(p.first,p.second);assert(!ReadCharacterSource(x.m,x.roots));
    }
    // A lazy renderer or an unregistered source is not a pointer to dereference.
    {CharacterFixture x;x.m.Word(0x49000,0);auto v=ReadCharacterSource(x.m,x.roots);assert(v && !v->meshes[0].renderer);}
    {CharacterFixture x;x.m.Word(0x44008,0);auto v=ReadCharacterSource(x.m,x.roots);assert(v && !v->meshes[0].renderer);}
    {CharacterFixture x;x.m.Word(0x46008,0);auto v=ReadCharacterSource(x.m,x.roots);assert(v && !v->meshes[0].materialOverride);}
    for(uint32_t field:{0x95000u,0x40074u,0x421f0u,0x43010u,0x44004u,0x4402cu,0x44008u,
            0x44034u,0x45004u,0x46008u,0x48000u,0x49000u}){
        CharacterFixture x;ChangedCharacterRead changed(x.m,field);assert(!ReadCharacterSource(changed,x.roots));
    }
    std::puts("Character source: ownership, bone bounds, one-based renderer IDs, lazy slots, interrupted reads and changing identities passed");
}
