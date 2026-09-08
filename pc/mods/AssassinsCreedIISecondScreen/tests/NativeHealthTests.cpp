#include "InventoryFixture.h"
#include "NativeHealth.h"
#include <set>
struct HealthFixture {
    OwnedFixture owned;MemoryFixture& m=owned.memory;
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    HealthFixture(){
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Word(0x4304c,0x98000);m.Word(0x98014,0x99000);m.Word(0x98038,3);
        constexpr uint32_t functions[]{0x15fde10,0x15fde30,0x15fde70};
        for(uint32_t i=0;i<3;++i){
            m.Word(0x99004+i*24,((8000+i)<<3)|5);m.Word(0x99008+i*24,0x9a000+i*32);
            m.Word(0x99014+i*24,functions[i]);m.Word(0x9a00c+i*32,0x9b000);
        }
        m.Word(0x9b000,0x1ea1658);m.Word(0x9b020,0x43000);
        m.Word(0x9b058,6);m.Word(0x9b05c,10);m.Word(0x9b060,2);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
};
int main(){
    HealthFixture f;auto health=ReadHealth(f.m,f.roots);assert(health);
    assert(health->current==6 && health->maximum==10 && health->RestorableMaximum()==8);
    const auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){HealthFixture broken;broken.m.failAt=i;assert(!ReadHealth(broken.m,broken.roots));}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x98038,4097},{0x98038,2},{0x99004,0x80000005},
            {0x99008,0},{0x9902c,0x15fde10},{0x99004,4},{0x9b000,0},{0x9b020,0},
            {0x9b058,UINT32_MAX},{0x9b05c,0},{0x9b05c,4097},{0x9b060,11},{0x400f4,4},{0x95000,0}}){
        HealthFixture broken;broken.m.Word(change.first,change.second);assert(!ReadHealth(broken.m,broken.roots));
    }
    HealthFixture dead;dead.m.Word(0x9b058,0);assert(ReadHealth(dead.m,dead.roots)->current==0);
    for(uint32_t page:{0x99000u,0x9b000u,0x95000u}){
        HealthFixture changed;changed.m.mutateAddress=page;assert(!ReadHealth(changed.m,changed.roots));
    }
    std::puts("Native health: shared subscriber ownership, damaged capacity, dead player and coherence guards passed");
}
