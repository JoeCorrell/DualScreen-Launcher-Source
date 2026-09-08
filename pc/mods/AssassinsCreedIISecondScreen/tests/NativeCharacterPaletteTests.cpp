#include "NativeCharacterFixture.h"
#include "NativeCharacterPalette.h"
struct PaletteFixture:CharacterFixture{
    PaletteFixture(){
        m.Array(0x51024,0x52000,3);m.Array(0x4a034,0x53000,1);
        m.Word(0x53000,2);m.Word(0x53004,0x52000);m.Word(0x53008,0x52030);
        m.Word(0x530ac,0x0000ff00);m.Word(0x52000,0x12345678);m.Word(0x52030,0x87654321);
        std::set<uint32_t> pages;for(auto b:m.bytes)pages.insert(b.first&~4095u);
        for(auto p:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(p+i,0);
    }
    std::optional<std::vector<CharacterPalette>> Read(const Memory& input){
        ReadTransaction memory(input);auto source=ReadCharacterSourceFields(memory,roots);if(!source)return {};
        std::vector<CharacterPalette> result;
        if(!ReadCharacterPaletteFields(memory,source->meshes[0],result) || !memory.Validate())return {};
        return result;
    }
};
int main(){
    PaletteFixture f;auto p=f.Read(f.m);assert(p && p->size()==1 && (*p)[0].size()==2);
    assert((*p)[0][0].hasBindRecord && (*p)[0][0].skeletonIndex==0 && (*p)[0][0].bindRecord[0]==0x78);
    assert((*p)[0][1].skeletonIndex==255 && (*p)[0][1].bindRecord[0]==0x21);
    auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){PaletteFixture x;x.m.failAt=i;assert(!x.Read(x.m));}
    for(auto item:{std::pair<uint32_t,uint32_t>{0x53000,43},{0x53004,0},{0x53004,0x52001},
            {0x53004,0x52048},{0x530ac,1},{0x51028,2u<<16},{0x4a038,257u<<16},{0x4a034,0xfffffff0}}){
        PaletteFixture x;x.m.Word(item.first,item.second);assert(!x.Read(x.m));
    }
    for(uint32_t field:{0x52000u,0x53004u,0x530acu,0x45000u,0x51024u}){
        PaletteFixture x;ChangedCharacterRead changed(x.m,field);assert(!x.Read(changed));
    }
    std::puts("Character palettes: bind ownership, skeleton mapping, bounded records and coherence passed");
}
