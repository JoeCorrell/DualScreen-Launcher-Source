#include "../src/NativeSuitInputRead.h"
#include <cassert>
#include <map>
#include <vector>

namespace {
constexpr uint32_t Root=0x100000, Actor=0x200000, Receiver=0x300000, Entity=0x7777;
struct Memory {
    std::map<uint32_t,std::vector<uint8_t>> blocks;
    uint32_t receiverReads{};
    bool replace{}, fail{};
    void Word(uint32_t address,uint32_t value) {
        std::vector<uint8_t> bytes(4); std::memcpy(bytes.data(),&value,4); blocks[address]=bytes;
    }
    bool operator()(uint32_t address,void* out,size_t size) {
        if(fail) return false;
        if(address==Actor+0x1108 && ++receiverReads==2 && replace) Word(address,0);
        for(size_t i=0;i<size;++i) {
            const uint32_t current=address+static_cast<uint32_t>(i);
            auto it=blocks.upper_bound(current);
            if(it==blocks.begin()) return false;
            --it;
            if(current-it->first>=it->second.size()) return false;
            static_cast<uint8_t*>(out)[i]=it->second[current-it->first];
        }
        return true;
    }
    Memory() {
        Word(c3native::actor_detail::RootSlot,Root);
        Word(Root,c3native::actor_detail::RootVtable);
        Word(Actor,c3native::actor_detail::ActorVtable); Word(Actor+8,Entity);
        Word(Actor+0x1108,Receiver);
        Word(Receiver,0x399b63b8); Word(Receiver+4,0x399b63a8); Word(Receiver+0x2d8,Actor);
    }
};
}
int main() {
    using namespace c3native;
    ActorFrameGuard guard{1,7,7,0,true,true};
    NativeSuitInputObservation out{};
    auto run=[&](Memory& memory,bool verified=true) {
        out.receiverPresent=true;
        return ReadSuitInput(memory,Root,Actor,Entity,verified,guard,[&]{return guard;},out);
    };
    {Memory m; assert(run(m)==SuitInputReadFailure::None && out.receiverPresent);}
    {Memory m; m.Word(Actor+0x1108,0); assert(run(m)==SuitInputReadFailure::None && !out.receiverPresent);}
    {Memory m; assert(run(m,false)==SuitInputReadFailure::GuardUnverified && !out.receiverPresent);}
    {Memory m; m.replace=true; assert(run(m)==SuitInputReadFailure::ChangedDuringRead && !out.receiverPresent);}
    {Memory m; m.Word(Receiver+0x2d8,Actor+4); assert(run(m)==SuitInputReadFailure::IdentityMismatch && !out.receiverPresent);}
    {Memory m; m.Word(Receiver,0x399b63a8); assert(run(m)==SuitInputReadFailure::UnsupportedLayout && !out.receiverPresent);}
    {Memory m; m.Word(Actor+0x1108,0xfffffffcu); assert(run(m)==SuitInputReadFailure::ReadFailed && !out.receiverPresent);}
    {Memory m; m.fail=true; assert(run(m)==SuitInputReadFailure::ReadFailed && !out.receiverPresent);}
    {Memory m; auto changed=guard; ++changed.epoch;
        assert(ReadSuitInput(m,Root,Actor,Entity,true,guard,[&]{return changed;},out)==SuitInputReadFailure::ChangedDuringRead);
        assert(!out.receiverPresent);}
}
