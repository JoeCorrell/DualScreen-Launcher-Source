#include "NativeEquipmentFixture.h"
#include "NativeWeaponStats.h"
#include <set>
struct WeaponFixture {
    EquipmentFixture equipment;MemoryFixture& m=equipment.m;
    ac2ds::native::Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    ac2ds::native::EquipmentRecord item;
    WeaponFixture(){
        item.id=777;item.menu=5;item.definitionHandle=0x51000;
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Word(0x223c590,0x6c000);m.Word(0x6c000,0x1e3c920);m.Array(0x6c178,0x6e000,1);
        m.Word(0x6e000,0x51000);m.Word(0x6e004,0x70030);equipment.Ref(0x70030,0x71000,8888);
        m.Word(0x71004,4);m.Array(0x71074,0x73000,2);
        m.Word(0x73000,0);m.Word(0x73004,0x74000);m.Word(0x74000,0x1e5b2d0);
        m.bytes[0x74020]=4;m.bytes[0x74021]=2;m.bytes[0x74022]=3;
        m.Array(0x2214ec4,0x75000,1);m.Word(0x75000,0x70000);m.Word(0x75004,0x4000);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
    auto Read(){return ac2ds::native::ReadWeaponStats(m,roots,item);}
};
struct ChangingMemory final : ac2ds::native::Memory {
    MemoryFixture& source;uint32_t changed;mutable unsigned seen{};
    ChangingMemory(MemoryFixture& source_,uint32_t changed_):source(source_),changed(changed_){}
    bool Copy(uint32_t address,void* target,size_t bytes)const override {
        if(!source.Copy(address,target,bytes))return false;
        if(changed>=address && changed-address<bytes && ++seen>1)
            static_cast<uint8_t*>(target)[changed-address]^=1;
        return true;
    }
};
int main(){
    WeaponFixture f;auto stats=f.Read();assert(stats && stats->damage==3 && stats->speed==4 && stats->deflection==2);
    auto reads=f.m.reads;
    for(unsigned i=1;i<=reads;++i){WeaponFixture broken;broken.m.failAt=i;assert(!broken.Read());}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x52030,1},{0x1211c,0},{0x223c590,0},{0x6c000,0},
            {0x6c17c,4097u<<16},{0x6e000,0},{0x6e004,0},{0x70038,0},{0x71004,0},
            {0x71004,5},{0x75004,3},{0x2214ec8,0},{0x71078,257u<<16},
            {0x74000,0},{0x73000,0x74000},{0x95000,0}}){
        WeaponFixture broken;broken.m.Word(change.first,change.second);assert(!broken.Read());
    }
    for(auto address:{0x74020u,0x74021u,0x74022u}){
        WeaponFixture invalid;invalid.m.bytes[address]=6;assert(!invalid.Read());
        WeaponFixture zero;zero.m.bytes[address]=0;assert(zero.Read());
    }
    for(auto address:{0x51000u,0x51008u,0x5100cu,0x52030u,0x6e004u,0x70030u,0x70038u,
            0x71004u,0x71074u,0x73004u,0x74000u,0x74020u,0x75000u,0x95000u}){
        WeaponFixture changed;ChangingMemory memory(changed.m,address);
        assert(!ac2ds::native::ReadWeaponStats(memory,changed.roots,changed.item));
    }
    WeaponFixture retained;ChangingMemory retain(retained.m,0x70034);
    assert(ac2ds::native::ReadWeaponStats(retain,retained.roots,retained.item));
    // Native table order matters: later definition match replaces an earlier one.
    WeaponFixture duplicate;duplicate.m.Array(0x6c178,0x6e000,2);
    duplicate.m.Word(0x6e008,0x51000);duplicate.m.Word(0x6e00c,0x70030);assert(duplicate.Read());
    duplicate.m.Word(0x6e00c,0);assert(!duplicate.Read());
    // Special values are original executable branches, never a missing-data fallback.
    for(auto id:{0x0bcd04a6u,0x26740bd7u}){
        WeaponFixture special;special.item.id=id;special.m.Word(0x5100c,id);special.m.Word(0x223c590,0);
        auto value=special.Read();assert(value && value->speed==5 && value->deflection==1 &&
            value->damage==(id==0x0bcd04a6?1u:3u));
        special.m.Word(0x52030,1);assert(!special.Read());
    }
    WeaponFixture forged;forged.item.id=0x0bcd04a6;assert(!forged.Read());
    std::puts("Native weapon stats: ownership, native field order, resource pool identity and transition guards passed");
}
