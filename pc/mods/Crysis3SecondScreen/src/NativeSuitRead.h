#pragma once
// Exact-image, read-only SP module values. See SUIT-MODULE-CONTRACT.md.
#include "NativeActorRead.h"

namespace c3native {
struct NativeSuitModule {
    std::array<char,32> id{}, category{}, moduleClass{};
    uint32_t slot{}, cost{};
    bool purchased{}, unlocked{}, unlockedKnown{}, installed{}, maximum{};
};
struct NativeSuitModules {
    std::array<NativeSuitModule,16> modules{};
    uint32_t count{};
    int32_t upgradePoints{}; // Native menu clamps negative initialization value to zero.
};
enum class SuitReadFailure { None, GuardUnverified, ReadFailed, IdentityMismatch,
    UnsupportedLayout, ChangedDuringRead };
namespace suit_detail {
struct Raw {
    uint32_t root{}, actor{}, entity{}, manager{}, stats{}, points{};
    std::array<uint32_t,3> header{};
    uint64_t active{};
    std::array<uint32_t,2> tail{}; // module-index bitset; points cache
    std::array<uint8_t,2432> records{};
    bool Same(const Raw& r) const noexcept {
        return root==r.root && actor==r.actor && entity==r.entity && manager==r.manager && stats==r.stats && points==r.points &&
            header==r.header && active==r.active && tail==r.tail && records==r.records;
    }
};
inline uint32_t U32(const uint8_t* p) noexcept { uint32_t v{}; std::memcpy(&v,p,4); return v; }
template<class Read> SuitReadFailure Once(Read& read,uint32_t root,uint32_t actor,uint32_t entity,Raw& o) {
    using actor_detail::At;
    uint32_t globalRoot{}, rootVt{}, actorVt{}, actorId{}, owned{}, global{}, statsVt{};
    if(!At(read,actor_detail::RootSlot,0,globalRoot) || !At(read,root,0,rootVt) ||
       !At(read,root,0x1f8,owned) || !At(read,0x3ba12080,0,global) ||
       !At(read,actor,0,actorVt) || !At(read,actor,8,actorId) ||
       !At(read,root,0xd4,o.stats) || !At(read,o.stats,0,statsVt) ||
       !At(read,o.stats,0x2e8,o.points)) return SuitReadFailure::ReadFailed;
    if(globalRoot!=root || !entity || actorId!=entity || !owned || owned!=global)
        return SuitReadFailure::IdentityMismatch;
    if(rootVt!=actor_detail::RootVtable || actorVt!=actor_detail::ActorVtable || statsVt!=0x39a1a6b4)
        return SuitReadFailure::UnsupportedLayout;
    if(!At(read,owned,0,o.header) || !At(read,owned,0xed4,o.tail) ||
       !At(read,actor,0x13c0,o.active)) return SuitReadFailure::ReadFailed;
    if(o.header[0]!=0x39a45a1c || o.header[1]!=0x39a454d0 || o.header[2]>16)
        return SuitReadFailure::UnsupportedLayout;
    o.root=root; o.actor=actor; o.entity=entity; o.manager=owned;
    if(o.header[2] && !actor_detail::Bytes(read,owned,0xc,o.records.data(),o.header[2]*152))
        return SuitReadFailure::ReadFailed;
    return SuitReadFailure::None;
}
// Deliberately supports only strings contained within their copied record.
// All 48 strings in the observed 16-module profile satisfy this constraint.
inline bool String(const uint8_t* record,uint32_t address,uint32_t offset,std::array<char,32>& out) {
    const uint32_t pointer=U32(record+offset);
    if(pointer<address || uint64_t(pointer)>=uint64_t(address)+152) return false;
    size_t start=pointer-address;
    for(size_t i=0;i<out.size() && start+i<152;++i) {
        const uint8_t ch=record[start+i];
        if(!ch) return i!=0;
        if(ch<0x20 || ch>0x7e || i+1==out.size()) return false;
        out[i]=static_cast<char>(ch);
    }
    return false;
}
inline SuitReadFailure Decode(const Raw& raw,NativeSuitModules& out) {
    NativeSuitModules value{}; value.count=raw.header[2];
    int32_t points{}; std::memcpy(&points,&raw.points,4);
    value.upgradePoints=points<0 ? 0 : points;
    for(uint32_t i=0;i<value.count;++i) {
        const auto* r=raw.records.data()+i*152; auto& module=value.modules[i];
        const uint64_t address=uint64_t(raw.manager)+0xc+i*152;
        if(address>UINT32_MAX || U32(r)!=0x399984a0 || U32(r+4)<1 || U32(r+4)>2 ||
           U32(r+8)>63 || (U32(r+4)==2 && U32(r+12)>63) || r[0x94]>3 ||
           r[0x95]>3 || r[0x96]>3 || r[0x97]>1 || U32(r+0x90)>INT32_MAX)
            return SuitReadFailure::UnsupportedLayout;
        if(!String(r,static_cast<uint32_t>(address),0x18,module.id) ||
           !String(r,static_cast<uint32_t>(address),0x44,module.category) ||
           !String(r,static_cast<uint32_t>(address),0x60,module.moduleClass))
            return SuitReadFailure::UnsupportedLayout;
        for(uint32_t j=0;j<i;++j) if(module.id==value.modules[j].id) return SuitReadFailure::UnsupportedLayout;
        module.slot=r[0x94]; module.cost=U32(r+0x90);
        module.purchased=(r[0x95]&1)!=0; module.maximum=(r[0x96]&1)!=0;
        module.unlockedKnown=raw.points==raw.tail[1];
        module.unlocked=module.unlockedKnown && r[0x97]!=0 && static_cast<uint32_t>(value.upgradePoints)>=module.cost;
        module.installed=(raw.active & (uint64_t{1}<<U32(r+8)))!=0;
    }
    out=value; return SuitReadFailure::None;
}
} // namespace suit_detail

// Caller attests current actor using NativeActorRead, joins identity and phase,
// and attests the suit-specific fingerprints in addition to the actor profile.
// Copies no game pointers into result and grants no command capabilities.
template<class Read,class GuardNow>
SuitReadFailure ReadSuitModules(Read& read,uint32_t root,uint32_t actor,uint32_t entity,
    bool suitProfile,const ActorFrameGuard& entry,GuardNow&& guardNow,NativeSuitModules& out) {
    out={};
    if(!suitProfile || !entry.Usable()) return SuitReadFailure::GuardUnverified;
    suit_detail::Raw first{}, last{};
    auto status=suit_detail::Once(read,root,actor,entity,first);
    if(status!=SuitReadFailure::None) return status;
    status=suit_detail::Once(read,root,actor,entity,last);
    if(status!=SuitReadFailure::None) return status;
    if(!first.Same(last) || !entry.Same(guardNow())) return SuitReadFailure::ChangedDuringRead;
    return suit_detail::Decode(first,out);
}
} // namespace c3native
