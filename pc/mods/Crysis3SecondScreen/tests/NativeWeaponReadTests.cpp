#include "../src/NativeWeaponRead.h"
#include "../src/NativeWeaponProfile.h"
#include "../src/NativeAttachmentRead.h"
#include "../src/ReadOnlyTrial.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <vector>
using namespace c3::research;
struct Memory {
    std::map<uint32_t,std::vector<uint8_t>> blocks;
    uint32_t mutate{}; size_t reads{};
    std::map<uint32_t,size_t> addressReads;
    std::function<void(Memory&,uint32_t,size_t)> afterCopy;
    bool operator()(uint32_t address,void* out,size_t n) {
        ++reads;
        const size_t at=++addressReads[address];
        auto it=blocks.find(address);
        if(it==blocks.end() || n>it->second.size()) return false;
        std::memcpy(out,it->second.data(),n);
        if(address==mutate) ++it->second[0];
        if(afterCopy)afterCopy(*this,address,at);
        return true;
    }
    void word(uint32_t address,size_t offset,uint32_t value) {
        auto& b=blocks[address]; if(b.size()<offset+4)b.resize(offset+4);
        for(size_t i=0;i<4;++i)b[offset+i]=uint8_t(value>>(i*8));
    }
};
Memory Fixture() {
    Memory m; m.blocks[0x100000].resize(0x98);
    m.word(0x100000,0,InventoryVtable);m.word(0x100000,8,0x7777);
    m.word(0x100000,0x8c,0xaa5);m.word(0x100000,0x2c,0x200000);
    m.blocks[0x200000].resize(32);
    m.word(0x200000,0x10,0x300000);m.word(0x200000,0x14,90);
    m.word(0x200000,0x1c,120);return m;
}
Memory ItemFixture() {
    Memory m;m.word(0x700000,0,ItemSystemVtable);m.word(0x700190,0,0x710000);
    m.blocks[0x710000].resize(24);m.word(0x710000,0x10,0xaa5);
    m.word(0x710000,0x14,0x720000);return m;
}
void RangeRegressions() {
    assert(WeaponRange(0x10000,1));assert(WeaponRange(0xfffffff0,16));
    assert(WeaponRange(0xfffff000,4096));
    assert(!WeaponRange(0xfffffff0,17));assert(!WeaponRange(0xffff,1));
    assert(!WeaponRange(0x10000,0));assert(!WeaponRange(0x10000,4097));
    auto m=Fixture();std::array<uint8_t,16> target{};
    assert(!WeaponCopy(m,0xfffffff0,target.data(),17) && m.reads==0);
    InventoryObservation inv{1,2,3,4,5,6,7,8};
    assert(!ReadInventory(m,0x100000,0,inv) && !inv.address && m.reads==0);
    assert(!ReadInventory(m,0xfffffff0,0x7777,inv) && m.reads==0);
    m.blocks[0x100000].resize(0x97);
    assert(!ReadInventory(m,0x100000,0x7777,inv) && !inv.owner);
}
void VectorRegressions() {
    for(bool attachments:{false,true}) {
        const size_t offset=attachments?0x1c:0x10;
        auto m=Fixture();InventoryObservation inv{};InventoryEntries entries{};
        m.word(0x100000,offset,0x600000);m.word(0x100000,offset+4,0x600400);
        m.blocks[0x600000].resize(1024);
        for(uint32_t i=0;i<256;++i)m.word(0x600000,i*4,0x80000000u+i);
        assert(ReadInventory(m,0x100000,0x7777,inv));
        assert(ReadInventoryEntries(m,inv,attachments,entries) && entries.count==256);
        assert(entries.values[0]==0x80000000u && entries.values[255]==0x800000ffu);
        m.word(0x100000,offset+4,0x600404);
        assert(!ReadInventory(m,0x100000,0x7777,inv));
        m.word(0x100000,offset,0x600001);m.word(0x100000,offset+4,0x600005);
        assert(!ReadInventory(m,0x100000,0x7777,inv));
        m.word(0x100000,offset,0x600000);m.word(0x100000,offset+4,0x600003);
        assert(!ReadInventory(m,0x100000,0x7777,inv));
        m.word(0x100000,offset,0xfffc);m.word(0x100000,offset+4,0x10000);
        assert(!ReadInventory(m,0x100000,0x7777,inv));
        m.word(0x100000,offset,0x600000);m.word(0x100000,offset+4,0x600008);
        assert(ReadInventory(m,0x100000,0x7777,inv));
        m.blocks[0x600000].resize(7);entries.count=99;
        assert(!ReadInventoryEntries(m,inv,attachments,entries) && entries.count==0);
        m.blocks[0x600000].resize(8);m.mutate=0x600000;
        assert(!ReadInventoryEntries(m,inv,attachments,entries));
        m.mutate=0;
        m.afterCopy=[](Memory& memory,uint32_t address,size_t) {
            if(address==0x600000)memory.word(0x100000,0x8c,0xaa6);
        };
        assert(!ReadInventoryEntries(m,inv,attachments,entries) && !entries.count);
    }
}
void ItemLookupRegressions() {
    uint32_t item=99;auto m=ItemFixture();
    assert(!ReadItemPointer(m,0x700000,0,item) && !item && !m.reads);
    m.word(0x700000,0,ItemSystemVtable+4);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    m=ItemFixture();m.blocks.erase(0x700190);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    m=ItemFixture();m.word(0x700190,0,0);
    assert(ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    m=ItemFixture();m.word(0x700190,0,0x70018c);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    m=ItemFixture();m.blocks[0x710000].resize(23);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    m=ItemFixture();m.word(0x710000,0x14,0xfffc);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    m=ItemFixture();m.word(0x710000,0x14,0xffffffff);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    m=ItemFixture();m.word(0x710000,0x14,0);
    assert(ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    m=ItemFixture();m.word(0x710000,8,0x710020);
    m.blocks[0x710020].resize(24);m.word(0x710020,0x10,0xaa5);
    m.word(0x710020,8,0x710000);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item) && !item);
    assert(m.addressReads[0x710000]==2 && m.addressReads[0x710020]==2);
    m=ItemFixture();m.mutate=0x710000;
    assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    for(uint32_t altered:{0x700000u,0x700190u}) {
        m=ItemFixture();
        m.afterCopy=[altered](Memory& memory,uint32_t address,size_t at) {
            if(address==0x710000 && at==2)memory.word(altered,0,0);
        };
        assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    }
    m=ItemFixture();
    m.afterCopy=[](Memory& memory,uint32_t address,size_t at) {
        if(address==0x710000 && at==2)memory.word(0x710000,0x14,0x730000);
    };
    assert(!ReadItemPointer(m,0x700000,0xaa5,item));
    // Cross the sign boundary: the original compares unsigned EntityIds.
    m=ItemFixture();m.word(0x710000,0x10,0x80000000u);
    m.word(0x710000,8,0x710020);m.word(0x710000,12,0x710040);
    m.blocks[0x710020].resize(24);m.word(0x710020,0x10,0x7fffffffu);
    m.word(0x710020,0x14,0x730000);
    m.blocks[0x710040].resize(24);m.word(0x710040,0x10,0xffffffffu);
    m.word(0x710040,0x14,0x740000);
    assert(ReadItemPointer(m,0x700000,0x7fffffffu,item) && item==0x730000);
    assert(ReadItemPointer(m,0x700000,0xffffffffu,item) && item==0x740000);
    for(uint32_t depth:{64u,65u}) {
        m=ItemFixture();
        for(uint32_t i=0;i<depth;++i) {
            uint32_t node=0x710000+i*32;
            m.blocks[node].resize(24);m.word(node,0x10,depth-i);
            m.word(node,0x14,0x720000);
            m.word(node,8,i+1<depth?node+32:0);
        }
        assert(ReadItemPointer(m,0x700000,1,item)==(depth==64));
        if(depth==64)assert(item==0x720000);
        else assert(!item && m.addressReads[0x710800]==0);
    }
}
void AmmoLookupRegressions() {
    InventoryObservation inv{};AmmoObservation ammo{};auto m=Fixture();
    assert(ReadInventory(m,0x100000,0x7777,inv));
    m.reads=0;ammo={1,2,3,true};
    assert(!ReadInventoryAmmo(m,inv,0,ammo) && !ammo.found && !m.reads);
    m.word(0x100000,0x2c,0);assert(ReadInventory(m,0x100000,0x7777,inv));
    assert(ReadInventoryAmmo(m,inv,0x300000,ammo) && !ammo.found);
    m.word(0x100000,0x2c,0x100028);assert(ReadInventory(m,0x100000,0x7777,inv));
    assert(!ReadInventoryAmmo(m,inv,0x300000,ammo));
    m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
    m.blocks[0x200000].resize(31);
    assert(!ReadInventoryAmmo(m,inv,0x300000,ammo));
    m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
    m.word(0x200000,0x14,0x80000000u);m.word(0x200000,0x1c,0xffffffffu);
    assert(ReadInventoryAmmo(m,inv,0x300000,ammo));
    assert(ammo.found && ammo.count==INT32_MIN && ammo.capacity==-1);
    m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
    m.word(0x200000,8,0x200020);m.blocks[0x200020].resize(32);
    m.word(0x200020,0x10,0x300000);m.word(0x200020,8,0x200000);
    assert(!ReadInventoryAmmo(m,inv,0x300000,ammo) && !ammo.found);
    assert(m.addressReads[0x200000]==2 && m.addressReads[0x200020]==2);
    for(size_t changedOffset:{size_t(8),size_t(0x8c),size_t(0x2c)}) {
        m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
        m.afterCopy=[changedOffset](Memory& memory,uint32_t address,size_t at) {
            if(address==0x200000 && at==2)memory.word(0x100000,changedOffset,0);
        };
        assert(!ReadInventoryAmmo(m,inv,0x300000,ammo) && !ammo.found);
    }
    m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
    m.afterCopy=[](Memory& memory,uint32_t address,size_t at) {
        if(address==0x200000 && at==2)memory.word(0x200000,0x14,91);
    };
    assert(!ReadInventoryAmmo(m,inv,0x300000,ammo));
    m=Fixture();m.word(0x200000,0x10,0x80000000u);
    m.word(0x200000,8,0x200020);m.word(0x200000,12,0x200040);
    m.blocks[0x200020].resize(32);m.word(0x200020,0x10,0x7fffffffu);
    m.word(0x200020,0x14,71);m.word(0x200020,0x1c,80);
    m.blocks[0x200040].resize(32);m.word(0x200040,0x10,0xffffffffu);
    m.word(0x200040,0x14,99);m.word(0x200040,0x1c,100);
    assert(ReadInventory(m,0x100000,0x7777,inv));
    assert(ReadInventoryAmmo(m,inv,0x7fffffffu,ammo) && ammo.found && ammo.count==71);
    assert(ReadInventoryAmmo(m,inv,0xffffffffu,ammo) && ammo.found && ammo.count==99);
    for(uint32_t depth:{64u,65u}) {
        m=Fixture();
        for(uint32_t i=0;i<depth;++i) {
            uint32_t node=0x200000+i*32;m.blocks[node].resize(32);
            m.word(node,0x10,0x300000+depth-i);m.word(node,0x14,90);
            m.word(node,0x1c,100);m.word(node,8,i+1<depth?node+32:0);
        }
        assert(ReadInventory(m,0x100000,0x7777,inv));
        assert(ReadInventoryAmmo(m,inv,0x300001,ammo)==(depth==64));
        if(depth==64)assert(ammo.found && ammo.count==90);
        else assert(!ammo.found && m.addressReads[0x200800]==0);
    }
}
Memory SingleFixture() {
    auto m=Fixture();
    m.blocks[0x800000].resize(0x44);m.word(0x800000,0,0x399b5940);
    m.word(0x800000,8,0x7777);m.word(0x800000,0x20,0x100);
    m.word(0x800000,0x40,0x100000);
    m.blocks[0x900000].resize(0x2e0);m.word(0x900000,0,0x399e3d08);
    m.word(0x900000,8,0xaa5);m.word(0x900000,0xc,0x980000);
    m.word(0x900000,0x14,0x920000);m.word(0x900000,0x104,0x800000);
    m.word(0x900000,0x100,0x7777);
    m.word(0x900000,0x108,0x910000);m.word(0x900000,0x1bc,0x399e3b70);
    m.word(0x900000,0x210,0x930000);m.word(0x900000,0x214,0x930004);
    m.word(0x900000,0x238,0x970000);m.word(0x900000,0x23c,0x970010);
    m.blocks[0x910000].resize(12);m.word(0x910000,0,0x399ba1d8);
    m.word(0x910000,4,1);m.word(0x910000,8,10);
    m.blocks[0x920030].resize(1);m.word(0x930000,0,0x940000);
    m.blocks[0x940000].resize(12);m.word(0x940000,0,0x399e6b28);
    m.word(0x940000,4,0x900000);m.word(0x940000,8,0x950000);
    m.blocks[0x950000].resize(8);m.word(0x950000,4,0x960000);
    m.blocks[0x960000].resize(0x98);m.word(0x960000,0x7c,0x300000);
    m.blocks[0x960000][0x8a]=9;
    m.blocks[0x970000].resize(16);m.word(0x970000,0,0x310000);
    m.word(0x970000,8,0x300000);m.word(0x970000,12,10);
    m.blocks[0x980000].resize(32);m.word(0x980000,0,0x398eccc0);
    m.word(0x980000,0xc,0xaa5);m.word(0x980000,0x1c,0x990000);
    m.blocks[0x990000].resize(12);m.word(0x990000,0,0x398f4638);
    m.word(0x990000,8,0x9a0000);
    m.blocks[0x300000].resize(12);m.word(0x300000,0,0x398f4638);
    m.word(0x300000,8,0x9b0000);
    m.blocks[0x9a0000].resize(64);m.blocks[0x9b0000].resize(64);
    std::memcpy(m.blocks[0x9a0000].data(),"Hammer",7);
    std::memcpy(m.blocks[0x9b0000].data(),"HPistolBullet",14);
    return m;
}
void RegularSingleRegressions() {
    RegularSingleObservation out{};auto m=SingleFixture();
    assert(!ReadRegularSingle(m,false,0x800000,0x7777,0x900000,out) && m.reads==0);
    assert(ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    assert(out.entityId==0xaa5 && out.ammoClass==0x300000);
    assert(out.loadedRounds==10 && out.clipLimit==9 && out.displayRounds==9 && out.overflowRounds==1);
    assert(std::string(out.weaponClassName.data())=="Hammer");
    assert(std::string(out.ammoClassName.data())=="HPistolBullet");
    m.word(0x970000,12,8);assert(ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    assert(out.loadedRounds==8 && out.displayRounds==8 && out.overflowRounds==0);
    m.word(0x970000,12,0);assert(ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    assert(out.loadedRounds==0 && out.displayRounds==0);
    // Original missing-class behavior is zero, not a guessed reserve quantity.
    m.word(0x970000,8,0x310004);assert(ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    assert(out.loadedRounds==0);
    struct Mutation {uint32_t address;size_t offset;uint32_t value;};
    for(auto mutation:std::array<Mutation,17>{{
        {0x800000,0,0x399b5944},{0x800000,8,0x7778},{0x800000,0x20,0},
        {0x900000,0,0x399e3d0c},{0x900000,8,0xaa6},{0x900000,0x104,0x800004},
        {0x900000,0x1bc,0x399e3b74},{0x900000,0x2dc,0xffffffff},
        {0x910000,4,0},{0x940000,0,0x399e6b2c},{0x940000,4,0x900004},
        {0x900000,0x214,0x930003},{0x900000,0x23c,0x970201},
        {0x970000,12,0xffffffff},{0x980000,0xc,0xaa6},
        {0x990000,0,0x398f463c},{0x300000,0,0x398f463c}}}) {
        m=SingleFixture();m.word(mutation.address,mutation.offset,mutation.value);
        assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
        assert(!out.entityId && !out.loadedRounds && out.weaponClassName[0]==0);
    }
    m=SingleFixture();m.blocks[0x920030][0]=1;
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    m=SingleFixture();m.blocks[0x960000][0x8a]=0xff;m.blocks[0x960000][0x8b]=0xff;
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    m=SingleFixture();m.word(0x970000,0,0x300000);
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    m=SingleFixture();m.word(0x970000,12,32768);
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    m=SingleFixture();m.blocks[0x9a0000].assign(64,'X');
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    m=SingleFixture();m.blocks[0x9b0000][0]=0x1f;
    assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out));
    for(auto mutation:std::array<Mutation,11>{{{0x100000,0x8c,0xaa6},
        {0x900000,0x104,0x800004},{0x910000,4,0},{0x940000,4,0x900004},
        {0x950000,4,0x960004},{0x960000,0x8a,8},{0x970000,12,11},
        {0x800000,0x40,0x100004},{0x930000,0,0x940004},
        {0x980000,0x1c,0x990004},{0x920030,0,1}}}) {
        m=SingleFixture();m.afterCopy=[mutation](Memory& memory,uint32_t address,size_t at) {
            if(address==0x9b0000 && at==2)memory.word(mutation.address,mutation.offset,mutation.value);
        };
        assert(!ReadRegularSingle(m,true,0x800000,0x7777,0x900000,out) && !out.entityId);
    }
}
Memory NameFixture() {
    auto m=SingleFixture();
    m.word(0x900000,0x100,0x7777);
    m.word(0x100000,0x10,0x600000);m.word(0x100000,0x14,0x600004);
    m.word(0x600000,0,0xaa5);
    m.word(0x700000,0,ItemSystemVtable);m.word(0x700190,0,0x710000);
    m.blocks[0x710000].resize(24);m.word(0x710000,0x10,0xaa5);m.word(0x710000,0x14,0x900000);
    return m;
}
void OwnedNameRegressions() {
    OwnedWeaponName out{};auto m=NameFixture();
    assert(!ReadOwnedWeaponName(m,false,0x800000,0x7777,0x700000,0xaa5,out) && !m.reads);
    assert(!ReadOwnedWeaponName(m,true,0x800000,0,0x700000,0xaa5,out) && !m.reads);
    assert(ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out));
    assert(out.entityId==0xaa5 && out.equipped && std::string(out.className.data())=="Hammer");
    m.word(0x100000,0x8c,0xabc);
    assert(ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out) && !out.equipped);
    for(size_t i=2;i<NativeWeaponNameProfileCount;++i) {
        m=NameFixture();m.word(0x900000,0,NativeWeaponNameProfile[i].address);
        // Generic naming must not consult Single mode, magazine, or parameters.
        m.blocks.erase(0x940000);m.blocks.erase(0x960000);m.blocks.erase(0x970000);
        assert(ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out));
        assert(m.addressReads[0x940000]==0 && m.addressReads[0x960000]==0);
    }
    struct Mutation {uint32_t address;size_t offset;uint32_t value;};
    for(const auto change:{Mutation{0x900000,0,0x399e8fc9},Mutation{0x900000,8,0xaab},
        Mutation{0x900000,0x100,0x7778},Mutation{0x900000,0x104,0x800004},
        Mutation{0x910000,4,0},Mutation{0x980000,0xc,0xaab},
        Mutation{0x600000,0,0xaab},Mutation{0x710000,0x14,0}}) {
        m=NameFixture();m.word(change.address,change.offset,change.value);
        assert(!ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out) && !out.entityId);
    }
    m=NameFixture();m.word(0x100000,0x14,0x600008);m.word(0x600000,4,0xaa5);
    assert(!ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out));
    for(const auto change:{Mutation{0x900000,0x100,0x7778},Mutation{0x800000,0x40,0x100004},
        Mutation{0x910000,4,0},Mutation{0x980000,0x1c,0x990004},
        Mutation{0x600000,0,0xaab},Mutation{0x100000,0x8c,0xaab},
        Mutation{0x710000,0x14,0x900004}}) {
        m=NameFixture();m.afterCopy=[change](Memory& memory,uint32_t address,size_t at) {
            if(address==0x9a0000 && at==2)memory.word(change.address,change.offset,change.value);
        };
        assert(!ReadOwnedWeaponName(m,true,0x800000,0x7777,0x700000,0xaa5,out) && !out.entityId);
    }
    OwnedWeaponList list{};m=NameFixture();
    assert(!ReadOwnedWeaponList(m,false,0x800000,0x7777,0x700000,list) && !m.reads);
    assert(ReadOwnedWeaponList(m,true,0x800000,0x7777,0x700000,list));
    assert(list.count==1 && !list.unavailable && list.currentItem==0xaa5 && list.weapons[0].equipped);
    m=NameFixture();m.word(0x900000,0,0x399e8fc9);
    assert(ReadOwnedWeaponList(m,true,0x800000,0x7777,0x700000,list));
    assert(!list.count && list.unavailable==1);
    m=NameFixture();m.word(0x100000,0x14,0x600008);m.word(0x600000,4,0xaa5);
    assert(!ReadOwnedWeaponList(m,true,0x800000,0x7777,0x700000,list) && !list.count);
    m=NameFixture();m.word(0x600000,0,0);
    assert(!ReadOwnedWeaponList(m,true,0x800000,0x7777,0x700000,list));
    m=NameFixture();m.word(0x100000,0x14,0x600084);m.blocks[0x600000].resize(132);
    assert(!ReadOwnedWeaponList(m,true,0x800000,0x7777,0x700000,list));
    assert(m.addressReads[0x700000]==0); // 33 entries rejected before item traversal
}
void AttachmentRegressions() {
    auto fixture=[] {
        auto m=NameFixture();
        m.blocks[0x920000].resize(16);
        m.word(0x920000,8,0xa00000);m.word(0x920000,12,0xa00074);
        m.blocks[0xa00000].resize(0x74);m.word(0xa00000,0,0x300000);
        m.word(0x900000,0x70,1);m.word(0x900000,0x74,0x300000);m.word(0x900000,0x78,0xbeef);
        m.word(0x100000,0x1c,0xa10000);m.word(0x100000,0x20,0xa10004);
        m.word(0xa10000,0,0x300000);
        std::memset(m.blocks[0x9b0000].data(),0,64);
        std::memcpy(m.blocks[0x9b0000].data(),"SilencerPistol",15);
        return m;
    };
    NativeAttachmentValues out{}; auto m=fixture();
    auto capture=[&] { return ReadOwnedAttachments(m,true,0x800000,0x7777,0x700000,0xaa5,out); };
    assert(!ReadOwnedAttachments(m,false,0x800000,0x7777,0x700000,0xaa5,out) && !m.reads);
    assert(capture() && out.count==1 && out.weaponId==0xaa5);
    assert(out.items[0].mounted && out.items[0].owned && out.items[0].nativeEnabled);
    assert(std::string(out.items[0].name.data())=="SilencerPistol");
    c3::Snapshot snapshot{};snapshot.readOnlyTrial=true;snapshot.weapons.known=true;snapshot.weapons.count=1;
    snapshot.weapons.entries[0].key=0xaa5;
    c3::TrialAttachments(snapshot,out);
    const auto& published=snapshot.weapons.entries[0].attachments;
    assert(published.known && published.complete && published.count==1);
    assert(published.entries[0].equipped.value && !published.entries[0].select.verified);
    m.word(0x100000,0x8c,0xabc);assert(capture()); // holstered, still owned
    m.word(0x900000,0x70,0);assert(capture() && !out.items[0].mounted);
    m.word(0x900000,0x1b4,1);assert(capture() && !out.items[0].nativeEnabled);
    for(const auto change:std::array<std::array<uint32_t,3>,6>{{
        {0x900000,0x70,5},{0x900000,0x74,0xdead},{0x900000,0x78,0},
        {0x920000,12,0xa00075},{0x920000,12,0xa00000+13*0x74},{0x900000,0x100,0x8888}}}) {
        m=fixture();m.word(change[0],change[1],change[2]);
        assert(!capture() && !out.weaponId && !out.count);
    }
    for(uint32_t address:{0x900000u,0xa00000u,0xa10000u,0x920000u}) {
        m=fixture();m.mutate=address;assert(!capture() && !out.count);
    }
    m=fixture();m.blocks[0x9b0000].assign(64,'A');assert(!capture());
}
int main() {
    AttachmentRegressions();
    static_assert(NativeWeaponProfileCount==33,"Review any read-profile change with capture tests");
    for(const auto& fingerprint:NativeWeaponProfile) {
        assert(fingerprint.size && fingerprint.size<=4096);
        assert(std::strlen(fingerprint.sha256)==64);
    }
    RangeRegressions();VectorRegressions();ItemLookupRegressions();AmmoLookupRegressions();
    RegularSingleRegressions();OwnedNameRegressions();
    InventoryObservation inv{};AmmoObservation ammo{};
    auto m=Fixture();
    assert(ReadInventory(m,0x100000,0x7777,inv));assert(inv.currentItem==0xaa5);
    assert(ReadInventoryAmmo(m,inv,0x300000,ammo));
    assert(ammo.found && ammo.count==90 && ammo.capacity==120);
    InventoryEntries entries{};
    assert(ReadInventoryEntries(m,inv,false,entries) && entries.count==0);
    m.word(0x100000,0x10,0x600000);m.word(0x100000,0x14,0x600008);
    m.word(0x600000,0,0xaa5);m.word(0x600000,4,0x10978);
    assert(ReadInventory(m,0x100000,0x7777,inv));
    assert(ReadInventoryEntries(m,inv,false,entries) && entries.count==2 && entries.values[0]==0xaa5);
    m.word(0x100000,0x14,0x600004);
    assert(!ReadInventoryEntries(m,inv,false,entries) && entries.count==0);
    m=Fixture();assert(ReadInventory(m,0x100000,0x7777,inv));
    assert(ReadInventoryAmmo(m,inv,0x300001,ammo) && !ammo.found);
    assert(ReadInventoryAmmo(m,inv,0x2fffff,ammo) && !ammo.found);
    m.word(0x200000,0x14,0);assert(ReadInventoryAmmo(m,inv,0x300000,ammo));
    assert(ammo.found && ammo.count==0);
    m.word(0x200000,0x14,0xffffffff);assert(ReadInventoryAmmo(m,inv,0x300000,ammo));
    assert(ammo.count==-1); // preserve, do not invent infinite-ammo semantics
    m.word(0x200000,8,0x200000);assert(!ReadInventoryAmmo(m,inv,0x300000,ammo));
    assert(!ammo.found);m=Fixture();m.mutate=0x200000;
    assert(!ReadInventoryAmmo(m,inv,0x300000,ammo));
    m=Fixture();m.word(0x100000,8,0x7778);
    assert(!ReadInventory(m,0x100000,0x7777,inv));assert(!inv.address);
    m=Fixture();m.word(0x100000,0x10,0x600000);m.word(0x100000,0x14,0x500000);
    assert(!ReadInventory(m,0x100000,0x7777,inv));
    m=Fixture();m.word(0x100000,0,InventoryVtable+4);
    assert(!ReadInventory(m,0x100000,0x7777,inv));
    m=Fixture();m.mutate=0x100000;
    assert(!ReadInventory(m,0x100000,0x7777,inv));
    assert(!WeaponRange(0xfffffff0,32));assert(!WeaponRange(0,32));
    m=Memory{};uint32_t item=99;
    m.word(0x700000,0,ItemSystemVtable);m.word(0x700190,0,0x710000);
    m.blocks[0x710000].resize(24);m.word(0x710000,0x10,0xaa5);
    m.word(0x710000,0x14,0x720000);
    assert(ReadItemPointer(m,0x700000,0xaa5,item) && item==0x720000);
    assert(ReadItemPointer(m,0x700000,0xaa6,item) && item==0);
    m.word(0x710000,8,0x710000);
    assert(!ReadItemPointer(m,0x700000,0xaa5,item) && item==0);
    assert(!ReadItemPointer(m,0xfffffff0,0xaa5,item));
    std::cout<<"Native weapon read tests passed (synthetic, no adapter authorization).\n";
}
