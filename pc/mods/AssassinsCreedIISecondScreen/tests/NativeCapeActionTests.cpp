#include "NativeCapeFixture.h"
#include "NativeCapeAction.h"
#include "NativeCapeCalls.h"
#include <functional>
struct ActionFixture : CapeFixture {
    Roots roots{0x90000,0x91000,0x92000,0x40000,0x93000};
    ActionFixture(){
        m.Word(0x2212490,roots.session);m.Word(0x2212570,roots.selection);m.Word(0x223f814,roots.progress);
        m.Word(0x910d0,UINT32_MAX);m.Word(0x9004c,roots.world);m.Word(0x9253c,0x94000);
        m.Word(0x94004,0x95000);m.Word(0x95000,roots.player);m.Word(0x2214d00,0x96000);m.Word(0x96024,0);
        m.Word(0x223f820,0x97000);m.Word(0x97000,0x1e60898);m.Word(0x970fe,0);
        m.Word(0x4304c,0x98000);m.Word(0x98014,0x99000);m.Word(0x98038,2);
        m.Word(0x223f664,8231);m.Word(0x223f66c,8233);
        for(uint32_t i=0;i<2;++i){m.Word(0x99004+i*24,((8231+i*2)<<3)|5);m.Word(0x99008+i*24,0x9a000);
            m.Word(0x99014+i*24,i?0x15fcc30:0x15fbc00);}
        m.Word(0x9a000,0x1ea141c);m.Word(0x9a00c,0x9b000);m.Word(0x9b000,0x1ea1448);m.Word(0x9b020,0x43000);
        std::set<uint32_t> pages;for(auto byte:m.bytes)pages.insert(byte.first&~4095u);
        for(auto page:pages)for(uint32_t i=0;i<4096;++i)m.bytes.emplace(page+i,0);
    }
};
struct Engine : CapeEngine {
    unsigned queries{},mutations{};bool allowed=true,accepted=true;std::function<void()> duringQuery;
    bool CanEquip(const CapeTarget& t)override {
        ++queries;assert(t.entity==0x43000 && t.handle==0x51020 && t.menu==23 && t.currentMenu==22);
        if(duringQuery)duringQuery();return allowed;
    }
    bool Equip(const CapeTarget& t)override {++mutations;assert(t.handle==0x51020);return accepted;}
};
static uint32_t permissionStatus{},mutationStatus{},permissionCalls{},mutationCalls{};
static uint32_t __attribute__((fastcall)) Permission(uint32_t* entity,void*,uint32_t count,const uint32_t* handle,uint8_t* allowed){
    assert(*entity==0x43000 && count==1 && *handle==0x51020);++permissionCalls;*allowed=1;return permissionStatus;
}
static uint32_t __attribute__((fastcall)) Mutation(uint32_t* entity,void*,uint32_t count,const uint32_t* handle,uint32_t immediate){
    assert(*entity==0x43000 && count==1 && *handle==0x51020 && immediate==0);++mutationCalls;return mutationStatus;
}
int main(){
    ActionFixture f;auto target=ResolveCapeTarget(f.m,f.roots,778);assert(target);
    auto calls=f.m.reads;
    for(unsigned i=1;i<=calls;++i){ActionFixture broken;broken.m.failAt=i;Engine e;
        assert(EquipCape(broken.m,broken.roots,778,e)==CapeActionResult::Unavailable && !e.queries && !e.mutations);}
    Engine success;assert(EquipCape(f.m,f.roots,778,success)==CapeActionResult::Dispatched && success.queries==1 && success.mutations==1);
    Engine denied;denied.allowed=false;assert(EquipCape(f.m,f.roots,778,denied)==CapeActionResult::Denied && !denied.mutations);
    for(uint32_t id:{0u,777u,779u}){Engine e;assert(EquipCape(f.m,f.roots,id,e)==CapeActionResult::Unavailable && !e.queries);}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x52070,1},{0x970fe,1},{0x68054,39},{0x52010,4},
            {0x95000,0},{0x97000,0},{0x400f4,4},{0x1211c,0},{0x98038,1},{0x99008,0},{0x9902c,0},{0x9b020,0}}){
        ActionFixture blocked;blocked.m.Word(change.first,change.second);Engine e;
        assert(EquipCape(blocked.m,blocked.roots,778,e)==CapeActionResult::Unavailable && !e.queries && !e.mutations);
    }
    for(auto change:{std::pair<uint32_t,uint32_t>{0x52070,1},{0x970fe,1},{0x95000,0},{0x51028,0},{0x9b020,0},{0x98038,1}}){
        ActionFixture changed;Engine e;e.duringQuery=[&]{changed.m.Word(change.first,change.second);};
        assert(EquipCape(changed.m,changed.roots,778,e)==CapeActionResult::StateChanged && e.queries==1 && !e.mutations);
    }
    Engine rejected;rejected.accepted=false;
    assert(EquipCape(f.m,f.roots,778,rejected)==CapeActionResult::NativeRejected && rejected.mutations==1);
    CapeEventEngine nativeCalls(Permission,Mutation);
    // Repeated real x86 calls exercise ECX, stack cleanup, by-reference handle
    // and out-byte conventions, without ever entering game code in this test.
    for(int i=0;i<1000;++i){assert(nativeCalls.CanEquip(*target));assert(nativeCalls.Equip(*target));}
    assert(permissionCalls==1000 && mutationCalls==1000);
    permissionStatus=3;assert(!nativeCalls.CanEquip(*target));mutationStatus=3;assert(!nativeCalls.Equip(*target));
    std::puts("Cape actions: native permission ordering, ownership, blockers, callback transitions and one dispatch passed");
}
