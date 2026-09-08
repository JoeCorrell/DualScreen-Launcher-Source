#include "NativeEquipmentFixture.h"
#include "NativeWeaponSelection.h"
#include <set>
struct BladeFixture : EquipmentFixture {
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    BladeFixture(){
        m.Word(0x68014,6);
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Word(0x223f820,0x97000);m.Word(0x97000,0x1e60898);
        m.Word(0x4304c,0x98000);m.Word(0x98014,0x99000);m.Word(0x98038,6);
        constexpr uint32_t callbacks[]{0x15fde10,0x15fde30,0x15fde70,0x15f28a0,0x15f2780,0x15f2880};
        for(uint32_t i=0;i<6;++i){
            m.Word(0x99004+i*24,((8000+i)<<3)|5);m.Word(0x99008+i*24,i<3?0x9a000:0x9c000);
            m.Word(0x99014+i*24,callbacks[i]);
        }
        m.Word(0x9a00c,0x9b000);m.Word(0x9b000,0x1ea1658);m.Word(0x9b020,0x43000);
        m.Word(0x9b058,6);m.Word(0x9b05c,10);
        m.Word(0x223f72c,8003);m.Word(0x223f700,8004);m.Word(0x223f728,8005);
        m.Word(0x9c000,0x1ea12e0);m.Word(0x9c00c,0x9d000);m.Word(0x9d000,0x1ea1310);
        m.Word(0x9d020,0x43000);m.Word(0x9d05c,6);m.Word(0x9d061,1);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
};
struct BladeEngine final : WeaponSelectionEngine {
    unsigned requests{};bool accepted=true;
    bool Request(const WeaponSelectionTarget& target)override {
        ++requests;assert(target.entity==0x43000 && target.subscriber==0x9c000 && target.service==0x9d000);
        assert(target.id==777 && target.handle==0x51000);
        assert(target.primary==(target.menu==5?2u:target.menu==4?4u:1u));return accepted;
    }
};
struct ChangedMemory final : Memory {
    BladeFixture& f;unsigned after;uint32_t address,value;mutable unsigned reads{};
    ChangedMemory(BladeFixture& fixture,unsigned at,uint32_t a,uint32_t v):f(fixture),after(at),address(a),value(v){}
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(++reads==after)f.m.Word(address,value);return f.m.Copy(a,out,n);
    }
};
static unsigned requests;static uint32_t status,expectedPrimary=1;
static uint32_t __attribute__((fastcall)) NativeRequest(uint32_t* entity,void*,uint32_t primary){
    assert(*entity==0x43000 && primary==expectedPrimary);++requests;return status;
}
int main(){
    BladeFixture f;auto target=ResolveWeaponSelectionTarget(f.m,f.roots,777);assert(target);auto reads=f.m.reads;
    WeaponSelectionAction action;BladeEngine e;
    assert(action.Select(f.m,f.roots,777,100,e)==WeaponSelectionResult::Dispatched && e.requests==1);
    for(uint64_t now:{0u,100u,599u})assert(action.Select(f.m,f.roots,777,now,e)==WeaponSelectionResult::Cooldown && e.requests==1);
    assert(action.Select(f.m,f.roots,777,600,e)==WeaponSelectionResult::Dispatched && e.requests==2);
    BladeFixture upgrade;upgrade.m.Word(0x68014,7);WeaponSelectionAction upgraded;BladeEngine both;
    assert(upgraded.Select(upgrade.m,upgrade.roots,777,100,both)==WeaponSelectionResult::Dispatched);
    for(unsigned i=1;i<=reads;++i){BladeFixture broken;broken.m.failAt=i;WeaponSelectionAction a;BladeEngine engine;
        assert(a.Select(broken.m,broken.roots,777,100,engine)==WeaponSelectionResult::Unavailable && !engine.requests);}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x95000,0},{0x400f4,4},{0x97000,0},{0x970fb,1},{0x970fc,1},
            {0x970fe,1},{0x9d05c,1},{0x9d05c,7},{0x9d061,0},{0x9d061,2},{0x9d020,0},{0x9c000,0},
            {0x9d000,0},{0x99050,0},{0x9904c,4},{0x9905c,0},{0x98038,5},{0x98038,4097},
            {0x223f72c,8004},{0x223f700,65536},{0x9b058,0},{0x52030,1},{0x1211c,0},{0x68014,5},
            {0x68014,8},{0x51008,0}}){
        BladeFixture broken;broken.m.Word(change.first,change.second);WeaponSelectionAction a;BladeEngine engine;
        assert(a.Select(broken.m,broken.roots,777,100,engine)==WeaponSelectionResult::Unavailable && !engine.requests);
    }
    for(auto change:{std::pair<uint32_t,uint32_t>{0x95000,0},{0x9d05c,1},{0x9d061,0},{0x52030,1},
            {0x1211c,0},{0x68014,7},{0x9d020,0}}){
        BladeFixture changed;ChangedMemory memory(changed,reads+1,change.first,change.second);WeaponSelectionAction a;BladeEngine engine;
        assert(a.Select(memory,changed.roots,777,100,engine)==WeaponSelectionResult::StateChanged && !engine.requests);
    }
    for(uint32_t id:{0u,778u}){WeaponSelectionAction a;BladeEngine engine;
        assert(a.Select(f.m,f.roots,id,100,engine)==WeaponSelectionResult::Unavailable && !engine.requests);}
    BladeEngine reject;reject.accepted=false;WeaponSelectionAction rejected;
    assert(rejected.Select(f.m,f.roots,777,100,reject)==WeaponSelectionResult::NativeRejected && reject.requests==1);
    WeaponSelectionEventEngine native(NativeRequest);
    for(unsigned i=0;i<1000;++i)assert(native.Request(*target));
    assert(requests==1000);status=3;assert(!native.Request(*target));
    WeaponSelectionEventEngine absent(nullptr);assert(!absent.Request(*target));
    status=0;
    for(uint32_t menu:{4u,5u}){
        BladeFixture carried;uint32_t primary=menu==5?2:4,subtype=menu==5?1:2;
        carried.m.Word(0x68014,menu);carried.m.Word(0x52010,subtype);carried.m.bytes[0x9d060+primary]=1;
        auto selected=ResolveWeaponSelectionTarget(carried.m,carried.roots,777);assert(selected && selected->primary==primary);
        WeaponSelectionAction select;BladeEngine engine;
        assert(select.Select(carried.m,carried.roots,777,100,engine)==WeaponSelectionResult::Dispatched && engine.requests==1);
        expectedPrimary=primary;for(unsigned i=0;i<1000;++i)assert(native.Request(*selected));
        carried.m.bytes[0x9d060+primary]=0;assert(!ResolveWeaponSelectionTarget(carried.m,carried.roots,777));
        carried.m.bytes[0x9d060+primary]=1;carried.m.Word(0x52010,subtype==1?2:1);
        assert(!ResolveWeaponSelectionTarget(carried.m,carried.roots,777));
    }
    // Owning another weapon in a slot must not expose a storage swap through
    // an API which only selects the currently carried slot.
    {
        BladeFixture multiple;multiple.m.Word(0x68014,5);multiple.m.Word(0x52010,1);multiple.m.bytes[0x9d062]=1;
        multiple.m.Array(0x12118,0x13000,2);multiple.m.Word(0x13004,0x50020);multiple.m.Word(0x50024,0x51010);
        multiple.Ref(0x51010,0x52040,778);multiple.m.Word(0x52040,0x1e699bc);multiple.m.Word(0x5204c,2);multiple.m.Word(0x52050,1);
        multiple.m.Array(0x65028,0x66000,2);multiple.m.Word(0x66004,0x67010);multiple.Ref(0x67010,0x68040);
        multiple.m.Word(0x68040,0x1e61020);multiple.m.Word(0x68048,0x51010);multiple.m.Word(0x68054,5);
        multiple.m.Word(0x6804c,0x69000);
        assert(ResolveWeaponSelectionTarget(multiple.m,multiple.roots,777));
        assert(!ResolveWeaponSelectionTarget(multiple.m,multiple.roots,778));
    }
    std::puts("Weapon selection: hidden/double blade, carried sword/dagger, native slot ABI, ownership, availability, stale state and cooldown passed");
}
