#include "NativeMission.h"
#include "NativeSynchronizationFixture.h"
struct MissionFixture : Fixture {
    mutable uint32_t change{};
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(!Fixture::Copy(a,out,n))return false;
        if(a==0xa0000 && change){pages[change&~4095u][change&4095]^=1;change=0;}
        return true;
    }
    MissionFixture(){
        Word(0x3000c,501);Word(0x4000c,601);Word(0x40054,701);Word(0x50018,5);
        Word(0x4010c,602);Word(0x40154,702);Word(0x50118,5);
        Word(0x7000c,801);Word(0x80018,5);
        Word(0x223f820,0x90000);Word(0x90000,0x1e60898);Word(0x90110,0xa0000);Word(0xa0000,901);
    }
};
int main(){
    MissionFixture f;auto r=ReadCurrentMission(f,f.roots);
    assert(r && r->present && !r->side && r->Identity()==0x100000001ull);
    assert(r->title==601 && r->description==701 && r->sequenceTitle==501 && r->objective==901);
    // Native priority is first story active, not highest index or newest value.
    f.Word(0x50018,6);r=ReadCurrentMission(f,f.roots);
    assert(r && r->present && r->index==1 && r->title==602);
    f.Word(0x50118,6);r=ReadCurrentMission(f,f.roots);
    assert(r && r->side && r->Identity()==0x8000000100000001ull && r->title==801 && !r->description);
    f.Word(0x80018,6);r=ReadCurrentMission(f,f.roots);assert(r && !r->present && !r->objective);
    // A stale UI text record alone never turns free roam into an active mission.
    f.Word(0xa0000,999);r=ReadCurrentMission(f,f.roots);assert(r && !r->present);
    f=MissionFixture{};f.Word(0xa0000,UINT32_MAX);r=ReadCurrentMission(f,f.roots);
    assert(r && r->present && !r->objective);
    f.Word(0x90110,0);r=ReadCurrentMission(f,f.roots);assert(r && r->present && !r->objective);
    f=MissionFixture{};f.Ref(0x42000,0);r=ReadCurrentMission(f,f.roots);assert(r && r->index==1);
    f=MissionFixture{};f.Word(0x50018,7);assert(!ReadCurrentMission(f,f.roots));
    f=MissionFixture{};f.Word(0x90000,0x1e6089c);assert(!ReadCurrentMission(f,f.roots));
    f=MissionFixture{};f.Word(0x5001c,101);assert(!ReadCurrentMission(f,f.roots));
    // Fail every actual underlying memory read, including final validation.
    f=MissionFixture{};assert(ReadCurrentMission(f,f.roots));const auto calls=f.calls;
    for(size_t i=1;i<=calls;++i){MissionFixture broken;broken.fail=i;assert(!ReadCurrentMission(broken,broken.roots));}
    for(uint32_t address:{0x2212490u,0x223f81cu,0x223f820u,0x19000u,0x21000u,0x32000u,
        0x42000u,0x50018u,0x5001cu,0x4000cu,0x90110u,0xa0000u}){
        MissionFixture changed;changed.change=address;assert(!ReadCurrentMission(changed,changed.roots));
    }
    MissionFixture neighbor;neighbor.change=0x50040;assert(ReadCurrentMission(neighbor,neighbor.roots));
    puts("Native current mission priority, objective clearing, and coherence tests passed");
}
