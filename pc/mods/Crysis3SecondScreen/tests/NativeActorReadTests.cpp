#include "../src/NativeActorRead.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>
using namespace c3native;
namespace {
constexpr uint32_t Root=0x100000,Framework=0x200000,Context=0x300000,Holder=0x400000;
constexpr uint32_t Channel=0x500000,Environment=0x600000,Branch=0x700000;
constexpr uint32_t Actor=0x800000,Suit=0x900000;
constexpr uint32_t Timer=0xa00000,Hud=0xb00000;
struct Memory {
    std::map<uint32_t,std::vector<uint8_t>> blocks;
    size_t calls{},rootReads{};
    bool changeActorOnSecond{},changeValueOnSecond{},changePhaseOnSecond{},invalidRequest{};
    void Block(uint32_t address,size_t size) { blocks[address]=std::vector<uint8_t>(size); }
    template<class T> void Put(uint32_t address,T value) {
        auto it=blocks.upper_bound(address); assert(it!=blocks.begin()); --it;
        const auto offset=address-it->first;
        assert(offset+sizeof(value)<=it->second.size());
        std::memcpy(it->second.data()+offset,&value,sizeof(value));
    }
    bool operator()(uint32_t address,void* output,size_t size) {
        ++calls;
        if(address<0x10000 || size>4096 || !size) { invalidRequest=true; return false; }
        if(address==actor_detail::RootSlot && ++rootReads==2) {
            if(changeActorOnSecond) Put(Context+0x8b4,uint32_t(Actor+4));
            if(changeValueOnSecond) Put(Suit+0x88,20.0f);
            if(changePhaseOnSecond) Put(Context+0x858,uint32_t(4));
        }
        auto it=blocks.upper_bound(address); if(it==blocks.begin()) return false; --it;
        const uint64_t offset=uint64_t(address)-it->first;
        if(offset+size>it->second.size()) return false;
        std::memcpy(output,it->second.data()+offset,size); return true;
    }
    Memory() {
        Block(actor_detail::RootSlot,4); Block(actor_detail::RefreshFlag,1);
        Block(Root,0x34); Block(Framework,0x2c); Block(Context,0x8b8); Block(Holder,12);
        Block(Channel,20); Block(Environment,64); Block(Branch,12);
        Block(actor_detail::BranchVtable+0xa8,4); Block(actor_detail::BranchGetter,4);
        Block(Actor,0xfc4); Block(Suit,0x1bc);
        Put(actor_detail::RootSlot,Root); Put(Root,actor_detail::RootVtable);
        Put(Root+0x30,Framework); Put(Framework,actor_detail::FrameworkVtable);
        Put(Framework+0x28,Context); Put(Context+0x24,Holder); Put(Holder+8,Channel);
        Put(Channel+0x10,uint32_t(0x7777)); Put(Context+0x2c,Environment);
        Put(Environment+0x3c,Branch); Put(Branch,actor_detail::BranchVtable);
        Put(Branch+8,uint32_t(0x6c000000)); // AL, not a full DWORD bool.
        Put(actor_detail::BranchVtable+0xa8,actor_detail::BranchGetter);
        Put(actor_detail::BranchGetter,uint32_t(0xc308418a));
        Put(Context+0x8b0,uint32_t(0x7777)); Put(Context+0x8b4,Actor);
        Put(Actor,actor_detail::ActorVtable); Put(Actor+8,uint32_t(0x7777));
        const float health=1000.0f; uint32_t bits{}; std::memcpy(&bits,&health,4);
        Put(Actor+0x24,bits^uint32_t(0x12345678)); Put(Actor+0x28,uint32_t(0x12345678));
        Put(Actor+0x2c,1000.0f); Put(Actor+0xfc0,Suit); Put(Suit+0x18,Actor);
        Put(Suit+0x80,uint32_t(2)); Put(Suit+0x84,uint32_t(0));
        Put(Suit+0x1b8,uint32_t(0)); Put(Suit+0x88,89.2461f); Put(Suit+0xc8,125.0f);
    }
    void AddPhase() {
        blocks[Root].resize(0x1ec); blocks[Environment].resize(0xaa);
        Block(0x3b8063dc,4); Block(Timer,0x69); Block(0x39952890,4); Block(0x37ce8800,4);
        Block(Hud,0x70); Put(Root+0x1e8,Hud); Put(Hud,uint32_t(0x399ef860));
        Put(Hud+4,uint32_t(0x399ef84c)); Put(Hud+8,uint32_t(0x399ef844));
        Put(Hud+0x6d,uint8_t(0)); Put(Hud+0x6e,uint8_t(1));
        Put(Context+0x858,uint32_t(5)); Put(Environment+0xa9,uint8_t(1));
        Put(0x3b8063dc,Timer); Put(Timer,uint32_t(0x39952878)); Put(Timer+0x68,uint8_t(1));
        Put(0x39952890,uint32_t(0x37ce8800)); Put(0x37ce8800,uint32_t(0xc368418a));
    }
};
ActorFrameGuard Good() { return {12,41,41,0,true,true}; }
ActorReadFailure Run(Memory& memory,NativeActorValues& values) {
    return ReadCurrentActor(memory,[]{return Good();},Root,values);
}
void MustFail(Memory& memory,ActorReadFailure expected) {
    NativeActorValues value{}; value.health=999; value.hasSuit=true;
    assert(Run(memory,value)==expected);
    assert(!value.entity && value.health==0 && !value.hasSuit && !memory.invalidRequest);
}
}
int main() {
    NativeActorValues value{};
    { Memory m; assert(Run(m,value)==ActorReadFailure::None); assert(value.entity==0x7777);
      assert(value.health==1000 && value.maxHealth==1000 && value.hasSuit);
      assert(value.mode==2 && value.channel==0 && std::fabs(value.energy-89.2461f)<0.0001f && value.maxEnergy==125); }
    { Memory m; m.Put(Suit+0x1b8,uint32_t(1)); m.Put(Suit+0xf8,35.0f); m.Put(Suit+0x138,80.0f);
      assert(Run(m,value)==ActorReadFailure::None && value.channel==1 && value.energy==35 && value.maxEnergy==80); }
    { Memory m; m.Put(Actor+0x28,uint32_t(0)); assert(Run(m,value)==ActorReadFailure::None && value.health==0); }
    { Memory m; m.Put(Actor+0xfc0,uint32_t(0)); assert(Run(m,value)==ActorReadFailure::None);
      assert(!value.hasSuit && value.health==1000 && value.maxEnergy==0); }
    { Memory m; m.Put(Branch+8,uint8_t(1)); MustFail(m,ActorReadFailure::AlternatePath); }
    { Memory m; m.Put(actor_detail::RefreshFlag,uint8_t(1)); MustFail(m,ActorReadFailure::CacheRefreshRequired); }
    { Memory m; m.Put(Context+0x8b0,uint32_t(123)); MustFail(m,ActorReadFailure::CacheRefreshRequired); }
    { Memory m; m.Put(Channel+0x10,uint32_t(0)); MustFail(m,ActorReadFailure::NoClient); }
    { Memory m; m.Put(Actor+8,uint32_t(123)); MustFail(m,ActorReadFailure::IdentityMismatch); }
    { Memory m; m.Put(Suit+0x18,uint32_t(123)); MustFail(m,ActorReadFailure::IdentityMismatch); }
    { Memory m; m.Put(actor_detail::BranchGetter,uint32_t(0xc308418b)); MustFail(m,ActorReadFailure::UnsupportedLayout); }
    { Memory m; m.Put(actor_detail::BranchVtable+0xa8,uint32_t(0x12345678)); MustFail(m,ActorReadFailure::UnsupportedLayout); }
    { Memory m; m.Put(Suit+0x80,uint32_t(3)); MustFail(m,ActorReadFailure::InvalidVitals); }
    { Memory m; m.Put(Suit+0x1b8,uint32_t(2)); MustFail(m,ActorReadFailure::InvalidVitals); }
    { Memory m; m.Put(Suit+0xc8,0.0f); MustFail(m,ActorReadFailure::InvalidVitals); }
    { Memory m; m.Put(Actor+0x2c,std::numeric_limits<float>::quiet_NaN()); MustFail(m,ActorReadFailure::InvalidVitals); }
    { Memory m; m.Put(Suit+0x88,-1.0f); MustFail(m,ActorReadFailure::InvalidVitals); }
    { Memory m; m.blocks.erase(Holder); MustFail(m,ActorReadFailure::ReadFailed); }
    { Memory m; m.Put(Context+0x24,uint32_t(0xfffffff8)); MustFail(m,ActorReadFailure::ReadFailed); }
    { Memory m; m.changeValueOnSecond=true; MustFail(m,ActorReadFailure::ChangedDuringRead); }
    { Memory m; m.changeActorOnSecond=true; MustFail(m,ActorReadFailure::ReadFailed); }
    { Memory m; unsigned checks=0; auto guard=[&]{auto v=Good(); v.epoch+=checks++; return v;};
      assert(ReadCurrentActor(m,guard,Root,value)==ActorReadFailure::ChangedDuringRead && !value.entity); }
    { Memory m; auto guard=[] { auto v=Good(); v.callbackThreadAttested=false; return v; };
      assert(ReadCurrentActor(m,guard,Root,value)==ActorReadFailure::GuardUnverified && m.calls==0); }
    { Memory m; auto guard=[] { auto v=Good(); v.currentThread=99; return v; };
      assert(ReadCurrentActor(m,guard,Root,value)==ActorReadFailure::GuardUnverified && m.calls==0); }
    { Memory m; auto guard=[] { auto v=Good(); v.systemEventDepth=1; return v; };
      assert(ReadCurrentActor(m,guard,Root,value)==ActorReadFailure::GuardUnverified && m.calls==0); }
    NativePhaseValues phase{};
    auto checkPhase=[&](Memory& m) { return ReadCurrentPhase(m,[]{return Good();},Root,phase); };
    { Memory m; m.AddPhase(); assert(checkPhase(m)==ActorReadFailure::None);
      assert(phase.NativeUpdatePredicatesPass() && phase.cutsceneKnown && !phase.cutscene && !phase.contextEstablishing); }
    { Memory m; m.AddPhase(); m.Put(Timer+0x68,uint8_t(0)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(phase.pauseKnown && phase.paused && !phase.NativeUpdatePredicatesPass()); }
    { Memory m; m.AddPhase(); m.Put(Framework+4,uint8_t(1)); m.blocks.erase(Timer);
      assert(checkPhase(m)==ActorReadFailure::None && phase.paused); } // native short circuit
    { Memory m; m.AddPhase(); m.Put(Framework+4,uint32_t(0x81000000));
      assert(checkPhase(m)==ActorReadFailure::None && !phase.paused); } // only low byte
    { Memory m; m.AddPhase(); m.Put(Context+0x858,uint32_t(3)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(phase.contextEstablishing && !phase.contextReady && !phase.NativeUpdatePredicatesPass()); }
    { Memory m; m.AddPhase(); m.Put(Context+0x858,uint32_t(6)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(!phase.contextEstablishing && !phase.contextReady); }
    { Memory m; m.AddPhase(); m.Put(Environment+0xa9,uint8_t(0)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(!phase.environmentActive && !phase.NativeUpdatePredicatesPass()); }
    { Memory m; m.AddPhase(); m.Put(Hud+0x6d,uint8_t(1)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(phase.cutsceneKnown && phase.cutscene); }
    { Memory m; m.AddPhase(); m.Put(Hud+0x6d,uint16_t(0x100)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(phase.cutsceneKnown && !phase.cutscene); } // next flag isn't cutscene
    { Memory m; m.AddPhase(); m.Put(Root+0x1e8,uint32_t(0)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(!phase.cutsceneKnown); }
    { Memory m; m.AddPhase(); m.Put(Hud+4,uint32_t(0)); assert(checkPhase(m)==ActorReadFailure::UnsupportedLayout);
      assert(!phase.cutsceneKnown && !phase.contextPresent); }
    { Memory m; m.AddPhase(); m.Put(0x37ce8800,uint32_t(0xc368418b));
      assert(checkPhase(m)==ActorReadFailure::UnsupportedLayout && !phase.pauseKnown); }
    { Memory m; m.AddPhase(); m.Put(Framework+0x28,uint32_t(0)); assert(checkPhase(m)==ActorReadFailure::None);
      assert(!phase.contextPresent && !phase.NativeUpdatePredicatesPass()); }
    { Memory m; m.AddPhase(); m.changePhaseOnSecond=true;
      assert(checkPhase(m)==ActorReadFailure::ChangedDuringRead && !phase.contextPresent); }
    { Memory m; m.AddPhase(); auto guard=[] {auto v=Good(); v.exactProfile=false; return v;};
      assert(ReadCurrentPhase(m,guard,Root,phase)==ActorReadFailure::GuardUnverified && m.calls==0); }
    std::puts("Native actor/phase readers: 40 synthetic cases passed; runtime/phase validation not implied.");
}
