#pragma once
// Read-only, exact-build actor/vitals contract. See ACTOR-CONTRACT.md.
// Coherent copied values are NOT proof of gameplay phase or a verified adapter.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace c3native {
struct ActorFrameGuard {
    uint64_t epoch{};
    uint32_t callbackThread{}, currentThread{}, systemEventDepth{};
    bool exactProfile{}, callbackThreadAttested{};
    bool Usable() const noexcept {
        return exactProfile && callbackThreadAttested && epoch && callbackThread &&
            callbackThread==currentThread && systemEventDepth==0;
    }
    bool Same(const ActorFrameGuard& rhs) const noexcept {
        return Usable() && rhs.Usable() && epoch==rhs.epoch &&
            callbackThread==rhs.callbackThread && currentThread==rhs.currentThread;
    }
};

enum class ActorReadFailure {
    None, GuardUnverified, ReadFailed, UnsupportedLayout, NoClient,
    AlternatePath, CacheRefreshRequired, IdentityMismatch, InvalidVitals,
    ChangedDuringRead
};

struct NativeActorValues {
    uint32_t entity{}, mode{}, channel{};
    float health{}, maxHealth{}, energy{}, maxEnergy{};
    bool hasSuit{};
    // No pointers, guessed statistics, phase labels, or adapterVerified flag.
};

struct NativePhaseValues {
    uint32_t contextState{};
    bool contextPresent{}, contextReady{}, contextEstablishing{}, environmentActive{};
    bool pauseKnown{}, paused{}, cutsceneKnown{}, cutscene{};
    // This reproduces native update predicates, not a complete UI-phase model.
    // In-game menu ownership and transition attestation remain separate gates.
    bool NativeUpdatePredicatesPass() const noexcept {
        return contextPresent && contextReady && environmentActive && pauseKnown && !paused;
    }
};

namespace actor_detail {
constexpr uint32_t RootSlot=0x3b9fde30, RefreshFlag=0x3b806517;
constexpr uint32_t RootVtable=0x399bef80, FrameworkVtable=0x398ba5e0;
constexpr uint32_t ActorVtable=0x399b5940, BranchVtable=0x3991ca90;
constexpr uint32_t BranchGetter=0x385eeb80;

template<class Read> bool Bytes(Read& read,uint32_t base,uint32_t offset,void* out,size_t size) {
    const uint64_t address=uint64_t(base)+offset;
    if(base<0x10000 || !size || size>4096 || address>UINT32_MAX ||
        uint64_t(size)>uint64_t(UINT32_MAX)+1-address) return false;
    return read(static_cast<uint32_t>(address),out,size);
}
template<class Read,class T> bool At(Read& read,uint32_t base,uint32_t offset,T& out) {
    return Bytes(read,base,offset,&out,sizeof(out));
}
inline float Float(uint32_t bits) noexcept {
    float value{}; std::memcpy(&value,&bits,sizeof(value)); return value;
}
inline bool Nonnegative(float value) noexcept { return std::isfinite(value) && value>=0; }

struct RawActorFrame {
    // All are copied integer identities. Never use these outside this read.
    std::array<uint32_t,11> identity{};
    std::array<uint32_t,7> vitals{}; // encoded health,key,max,mode,channel,energy,maxEnergy
    uint32_t entity{};
    bool hasSuit{};
    bool Same(const RawActorFrame& rhs) const noexcept {
        return identity==rhs.identity && vitals==rhs.vitals &&
            entity==rhs.entity && hasSuit==rhs.hasSuit;
    }
};

struct RawPhaseFrame {
    std::array<uint32_t,6> identity{}; // root/framework/context/environment/timer/HUD
    std::array<uint32_t,7> flags{}; // state/context/active/pauseKnown/paused/cutsceneKnown/cutscene
    bool Same(const RawPhaseFrame& rhs) const noexcept { return identity==rhs.identity && flags==rhs.flags; }
};

template<class Read>
ActorReadFailure PhaseOnce(Read& read,uint32_t expectedRoot,RawPhaseFrame& out) {
    uint32_t root{},framework{},context{},environment{},timer{},hud{},vt{},getter{},state{};
    std::array<uint32_t,13> rootHeader{};
    std::array<uint32_t,11> frameworkHeader{};
    std::array<uint32_t,28> hudHeader{};
    std::array<uint8_t,4> timerCode{};
    uint8_t environmentActive{},timerEnabled{};
    if(!At(read,RootSlot,0,root)) return ActorReadFailure::ReadFailed;
    if(!root || root!=expectedRoot) return ActorReadFailure::NoClient;
    if(!At(read,root,0,rootHeader)) return ActorReadFailure::ReadFailed;
    if(rootHeader[0]!=RootVtable) return ActorReadFailure::UnsupportedLayout;
    framework=rootHeader[0x30/4];
    if(!framework) return ActorReadFailure::NoClient;
    if(!At(read,framework,0,frameworkHeader)) return ActorReadFailure::ReadFailed;
    if(frameworkHeader[0]!=FrameworkVtable) return ActorReadFailure::UnsupportedLayout;
    context=frameworkHeader[0x28/4];
    if(context) {
        if(!At(read,context,0x858,state) || !At(read,context,0x2c,environment))
            return ActorReadFailure::ReadFailed;
        if(environment && !At(read,environment,0xa9,environmentActive)) return ActorReadFailure::ReadFailed;
    }
    // Native +1f4 short-circuits on the low byte at framework+4.
    bool paused=(frameworkHeader[1]&0xff)!=0;
    if(!paused) {
        if(!At(read,0x3b8063dc,0,timer) || !At(read,timer,0,vt)) return ActorReadFailure::ReadFailed;
        if(vt!=0x39952878) return ActorReadFailure::UnsupportedLayout;
        if(!At(read,vt,0x18,getter) || !At(read,0x37ce8800,0,timerCode)) return ActorReadFailure::ReadFailed;
        if(getter!=0x37ce8800 || timerCode!=std::array<uint8_t,4>{0x8a,0x41,0x68,0xc3})
            return ActorReadFailure::UnsupportedLayout;
        if(!At(read,timer,0x68,timerEnabled)) return ActorReadFailure::ReadFailed;
        paused=timerEnabled==0;
    }
    if(!At(read,root,0x1e8,hud)) return ActorReadFailure::ReadFailed;
    bool cutsceneKnown=false,cutscene=false;
    if(hud) {
        if(!At(read,hud,0,hudHeader)) return ActorReadFailure::ReadFailed;
        if(hudHeader[0]!=0x399ef860 || hudHeader[1]!=0x399ef84c || hudHeader[2]!=0x399ef844)
            return ActorReadFailure::UnsupportedLayout;
        cutsceneKnown=true; cutscene=((hudHeader[0x6c/4]>>8)&0xff)!=0;
    }
    out.identity={root,framework,context,environment,timer,hud};
    out.flags={state,uint32_t(context!=0),uint32_t(environmentActive!=0),1,
        uint32_t(paused),uint32_t(cutsceneKnown),uint32_t(cutscene)};
    return ActorReadFailure::None;
}

template<class Read>
ActorReadFailure Once(Read& read,uint32_t expectedRoot,RawActorFrame& out) {
    uint32_t root{}, framework{}, context{}, actor{}, suit{}, getter{};
    std::array<uint32_t,13> rootHeader{};
    std::array<uint32_t,11> frameworkHeader{};
    std::array<uint32_t,3> contextLinks{},holder{},branch{};
    std::array<uint32_t,5> channel{};
    std::array<uint32_t,16> environment{};
    std::array<uint32_t,2> cache{};
    std::array<uint32_t,14> actorHeader{};
    std::array<uint32_t,111> suitHeader{}; // through owner+0x1b8 inclusive
    std::array<uint8_t,4> branchCode{};
    uint8_t refresh{};
    if(!At(read,RootSlot,0,root)) return ActorReadFailure::ReadFailed;
    if(root!=expectedRoot || !root) return ActorReadFailure::NoClient;
    if(!At(read,root,0,rootHeader)) return ActorReadFailure::ReadFailed;
    if(rootHeader[0]!=RootVtable) return ActorReadFailure::UnsupportedLayout;
    framework=rootHeader[0x30/4];
    if(!framework) return ActorReadFailure::NoClient;
    if(!At(read,framework,0,frameworkHeader)) return ActorReadFailure::ReadFailed;
    if(frameworkHeader[0]!=FrameworkVtable) return ActorReadFailure::UnsupportedLayout;
    context=frameworkHeader[0x28/4];
    if(!context) return ActorReadFailure::NoClient;
    if(!At(read,context,0x24,contextLinks)) return ActorReadFailure::ReadFailed;
    if(!contextLinks[0]) return ActorReadFailure::NoClient;
    if(!At(read,contextLinks[0],0,holder)) return ActorReadFailure::ReadFailed;
    if(!holder[2]) return ActorReadFailure::NoClient;
    if(!At(read,holder[2],0,channel)) return ActorReadFailure::ReadFailed;
    if(!channel[4]) return ActorReadFailure::NoClient;
    if(!At(read,contextLinks[2],0,environment) ||
        !At(read,environment[0x3c/4],0,branch)) return ActorReadFailure::ReadFailed;
    if(branch[0]!=BranchVtable) return ActorReadFailure::UnsupportedLayout;
    if(!At(read,branch[0],0xa8,getter) || !At(read,BranchGetter,0,branchCode))
        return ActorReadFailure::ReadFailed;
    // Exact native predicate: mov al,byte ptr [ecx+8]; ret. Only the low byte.
    if(getter!=BranchGetter || branchCode!=std::array<uint8_t,4>{0x8a,0x41,0x08,0xc3})
        return ActorReadFailure::UnsupportedLayout;
    if((branch[2]&0xff)!=0) return ActorReadFailure::AlternatePath;
    if(!At(read,RefreshFlag,0,refresh) || !At(read,context,0x8b0,cache))
        return ActorReadFailure::ReadFailed;
    if(refresh || cache[0]!=channel[4]) return ActorReadFailure::CacheRefreshRequired;
    actor=cache[1];
    if(!actor) return ActorReadFailure::NoClient;
    if(!At(read,actor,0,actorHeader) || !At(read,actor,0xfc0,suit))
        return ActorReadFailure::ReadFailed;
    if(actorHeader[0]!=ActorVtable) return ActorReadFailure::UnsupportedLayout;
    if(actorHeader[2]!=channel[4]) return ActorReadFailure::IdentityMismatch;
    out.identity={root,framework,context,contextLinks[0],holder[2],contextLinks[2],
        environment[0x3c/4],actor,suit,actorHeader[0],branch[2]&0xff};
    out.entity=channel[4]; out.hasSuit=suit!=0;
    out.vitals[0]=actorHeader[9]; out.vitals[1]=actorHeader[10]; out.vitals[2]=actorHeader[11];
    const float health=Float(actorHeader[10]?actorHeader[9]^actorHeader[10]:0);
    const float maxHealth=Float(actorHeader[11]);
    if(!Nonnegative(health) || !std::isfinite(maxHealth) || maxHealth<=0)
        return ActorReadFailure::InvalidVitals;
    if(suit) {
        if(!At(read,suit,0,suitHeader)) return ActorReadFailure::ReadFailed;
        if(suitHeader[0x18/4]!=actor) return ActorReadFailure::IdentityMismatch;
        const uint32_t mode=suitHeader[0x80/4], index=suitHeader[0x1b8/4];
        if(mode>2 || index>1) return ActorReadFailure::InvalidVitals;
        out.vitals[3]=mode; out.vitals[4]=index;
        out.vitals[5]=suitHeader[(0x88+index*0x70)/4];
        out.vitals[6]=suitHeader[(0xc8+index*0x70)/4];
        if(!Nonnegative(Float(out.vitals[5])) || !std::isfinite(Float(out.vitals[6])) ||
            Float(out.vitals[6])<=0) return ActorReadFailure::InvalidVitals;
    }
    return ActorReadFailure::None;
}
} // namespace actor_detail

// Invoke only within an already verified callback scope, never on the UI/worker
// thread. Guard provider is called before and after; memory reader must copy
// exactly the requested bytes or return false. There are no engine calls/writes.
// Two equal reads reject observed changes, but do not prove absence of ABA reuse.
template<class Read,class Guard>
ActorReadFailure ReadCurrentActor(Read&& read,Guard&& guard,uint32_t expectedRoot,NativeActorValues& values) {
    values={};
    const auto before=guard();
    if(!before.Usable()) return ActorReadFailure::GuardUnverified;
    actor_detail::RawActorFrame first{},second{};
    auto result=actor_detail::Once(read,expectedRoot,first);
    if(result!=ActorReadFailure::None) return result;
    result=actor_detail::Once(read,expectedRoot,second);
    if(result!=ActorReadFailure::None) return result;
    if(!before.Same(guard()) || !first.Same(second)) return ActorReadFailure::ChangedDuringRead;
    values.entity=second.entity;
    values.health=actor_detail::Float(second.vitals[1]?second.vitals[0]^second.vitals[1]:0);
    values.maxHealth=actor_detail::Float(second.vitals[2]);
    values.hasSuit=second.hasSuit;
    if(values.hasSuit) {
        values.mode=second.vitals[3]; values.channel=second.vitals[4];
        values.energy=actor_detail::Float(second.vitals[5]);
        values.maxEnergy=actor_detail::Float(second.vitals[6]);
    }
    return ActorReadFailure::None;
}

// Same callback/provenance requirements as ReadCurrentActor. No raw heap address
// escapes. A null HUD gives cutsceneKnown=false, never a guessed false state.
template<class Read,class Guard>
ActorReadFailure ReadCurrentPhase(Read&& read,Guard&& guard,uint32_t expectedRoot,NativePhaseValues& values) {
    values={};
    const auto before=guard();
    if(!before.Usable()) return ActorReadFailure::GuardUnverified;
    actor_detail::RawPhaseFrame first{},second{};
    auto result=actor_detail::PhaseOnce(read,expectedRoot,first);
    if(result!=ActorReadFailure::None) return result;
    result=actor_detail::PhaseOnce(read,expectedRoot,second);
    if(result!=ActorReadFailure::None) return result;
    if(!before.Same(guard()) || !first.Same(second)) return ActorReadFailure::ChangedDuringRead;
    values.contextState=second.flags[0]; values.contextPresent=second.flags[1]!=0;
    values.contextReady=values.contextPresent && values.contextState==5;
    values.contextEstablishing=values.contextPresent && values.contextState>0 && values.contextState<5;
    values.environmentActive=second.flags[2]!=0; values.pauseKnown=second.flags[3]!=0;
    values.paused=second.flags[4]!=0; values.cutsceneKnown=second.flags[5]!=0;
    values.cutscene=second.flags[6]!=0;
    return ActorReadFailure::None;
}
} // namespace c3native
