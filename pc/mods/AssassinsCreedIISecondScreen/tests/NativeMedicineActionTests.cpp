#include "NativeMedicineFixture.h"
#include "NativeMedicineCalls.h"
struct Engine final : MedicineEngine {
    unsigned requests{};bool accepted=true;
    bool Request(const MedicineTarget& target)override {
        ++requests;assert(target.entity==0x43000 && target.subscriber==0x9c000 && target.service==0x9d000);
        assert(target.definition==0xaa000 && target.container==0x12000 && target.quantity==8);
        assert(target.health==6 && target.maximum==8);return accepted;
    }
};
struct Transition final : Memory {
    MedicineFixture& fixture;unsigned after;uint32_t address,value;mutable unsigned reads{};
    Transition(MedicineFixture& f,unsigned at,uint32_t a,uint32_t v):fixture(f),after(at),address(a),value(v){}
    bool Copy(uint32_t a,void* out,size_t n)const override {
        if(++reads==after)fixture.m.Word(address,value);
        return fixture.m.Copy(a,out,n);
    }
};
static unsigned nativeRequests;static uint32_t nativeStatus;
static uint32_t __attribute__((fastcall)) Request(uint32_t* entity,void*,uint32_t requested){
    assert(*entity==0x43000 && requested==1);++nativeRequests;return nativeStatus;
}
int main(){
    MedicineFixture f;auto target=ResolveMedicineTarget(f.m,f.roots);assert(target);auto reads=f.m.reads;
    MedicineAction action;Engine e;
    assert(action.Use(f.m,f.roots,100,e)==MedicineActionResult::Dispatched && e.requests==1);
    for(uint64_t time:{0u,100u,1099u})assert(action.Use(f.m,f.roots,time,e)==MedicineActionResult::Cooldown && e.requests==1);
    assert(action.Use(f.m,f.roots,1100,e)==MedicineActionResult::Dispatched && e.requests==2);
    for(unsigned i=1;i<=reads;++i){MedicineFixture broken;broken.m.failAt=i;MedicineAction a;Engine engine;
        assert(a.Use(broken.m,broken.roots,100,engine)==MedicineActionResult::Unavailable && !engine.requests);}
    for(auto change:{std::pair<uint32_t,uint32_t>{0x95000,0},{0x400f4,4},{0x97000,0},{0x970fb,1},
            {0x970fc,1},{0x970fe,1},{0x9d06c,0},{0x9d06c,2},{0x9c01c,1},{0x9c000,0},{0x9d000,0},
            {0x9d020,0},{0x98038,5},{0x98038,4097},{0x99050,0},{0x9904c,4},{0x9905c,0},
            {0x223f730,8004},{0x223f708,65536},{0x223f728,0},{0xaa030,1},{0xaa00c,1},
            {0xa9008,0},{0x12010,0},{0x2400c,0},{0x9b058,0},{0x9b058,8},{0x9b058,10},
            {0x9b05c,0},{0x9b060,11},{0x9b020,0}}){
        MedicineFixture broken;broken.m.Word(change.first,change.second);MedicineAction a;Engine engine;
        const auto result=a.Use(broken.m,broken.roots,100,engine);
        if(result!=MedicineActionResult::Unavailable || engine.requests)std::printf("Unexpected action after %08x=%08x result=%d requests=%u\n",change.first,change.second,int(result),engine.requests);
        assert(result==MedicineActionResult::Unavailable && !engine.requests);
    }
    for(auto change:{std::pair<uint32_t,uint32_t>{0x95000,0},{0x9c01c,1},{0x9d06c,0},{0x12010,7},
            {0x9b058,7},{0xaa030,1},{0x9d020,0}}){
        MedicineFixture changed;Transition memory(changed,reads+1,change.first,change.second);MedicineAction a;Engine engine;
        assert(a.Use(memory,changed.roots,100,engine)==MedicineActionResult::StateChanged && !engine.requests);
    }
    Engine rejected;rejected.accepted=false;MedicineAction denied;
    assert(denied.Use(f.m,f.roots,2000,rejected)==MedicineActionResult::NativeRejected && rejected.requests==1);
    assert(denied.Use(f.m,f.roots,2001,rejected)==MedicineActionResult::Cooldown && rejected.requests==1);
    MedicineEventEngine native(Request);
    for(unsigned i=0;i<1000;++i)assert(native.Request(*target));
    assert(nativeRequests==1000);nativeStatus=3;assert(!native.Request(*target));
    MedicineEventEngine absent(nullptr);assert(!absent.Request(*target));
    std::puts("Medicine actions: real request ABI, native availability polarity, owned quantity, damaged health, stale state, single dispatch and cooldown passed");
}
