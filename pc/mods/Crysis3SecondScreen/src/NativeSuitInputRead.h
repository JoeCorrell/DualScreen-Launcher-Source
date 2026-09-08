#pragma once
// Read-only preflight for the exact native input receiver. No action grants.
// Static provenance: SUIT-ACTIONS-CONTRACT.md and audit_suit_actions.py.
#include "NativeActorRead.h"

namespace c3native {
enum class SuitInputReadFailure { None, GuardUnverified, ReadFailed,
    IdentityMismatch, UnsupportedLayout, ChangedDuringRead };
struct NativeSuitInputObservation {
    bool receiverPresent{};
    // The receiver can be replaced independently of the player. No address is
    // exported: dispatch must resolve it again inside the accepted callback.
};
namespace suit_input_detail {
struct Raw {
    uint32_t root{}, rootVt{}, actorVt{}, actorId{}, receiver{};
    std::array<uint32_t,2> receiverVt{};
    uint32_t receiverActor{};
    bool Same(const Raw& r) const noexcept {
        return root==r.root && rootVt==r.rootVt && actorVt==r.actorVt &&
            actorId==r.actorId && receiver==r.receiver && receiverVt==r.receiverVt &&
            receiverActor==r.receiverActor;
    }
};
template<class Read>
SuitInputReadFailure Once(Read& read,uint32_t root,uint32_t actor,uint32_t entity,Raw& out) {
    using actor_detail::At;
    if(!At(read,actor_detail::RootSlot,0,out.root) || !At(read,root,0,out.rootVt) ||
       !At(read,actor,0,out.actorVt) || !At(read,actor,8,out.actorId) ||
       !At(read,actor,0x1108,out.receiver)) return SuitInputReadFailure::ReadFailed;
    if(!entity || out.root!=root || out.actorId!=entity)
        return SuitInputReadFailure::IdentityMismatch;
    if(out.rootVt!=actor_detail::RootVtable || out.actorVt!=actor_detail::ActorVtable)
        return SuitInputReadFailure::UnsupportedLayout;
    // A null receiver is a real transient state, not an address to dereference.
    if(!out.receiver) return SuitInputReadFailure::None;
    if(!At(read,out.receiver,0,out.receiverVt) || !At(read,out.receiver,0x2d8,out.receiverActor))
        return SuitInputReadFailure::ReadFailed;
    if(out.receiverVt[0]!=0x399b63b8 || out.receiverVt[1]!=0x399b63a8)
        return SuitInputReadFailure::UnsupportedLayout;
    if(out.receiverActor!=actor) return SuitInputReadFailure::IdentityMismatch;
    return SuitInputReadFailure::None;
}
} // namespace suit_input_detail

// Not called by NativeProbe yet: profile attestation and live receiver evidence
// remain required. The caller must join NativeActorRead identity/phase before
// AND after this read; repeated copies alone are not lifetime guarantees.
template<class Read,class GuardNow>
SuitInputReadFailure ReadSuitInput(Read& read,uint32_t root,uint32_t actor,uint32_t entity,
    bool inputProfile,const ActorFrameGuard& entry,GuardNow&& guardNow,
    NativeSuitInputObservation& out) {
    out={};
    if(!inputProfile || !entry.Usable()) return SuitInputReadFailure::GuardUnverified;
    suit_input_detail::Raw first{}, last{};
    auto result=suit_input_detail::Once(read,root,actor,entity,first);
    if(result!=SuitInputReadFailure::None) return result;
    result=suit_input_detail::Once(read,root,actor,entity,last);
    if(result!=SuitInputReadFailure::None) return result;
    if(!first.Same(last) || !entry.Same(guardNow())) return SuitInputReadFailure::ChangedDuringRead;
    out.receiverPresent=first.receiver!=0;
    return SuitInputReadFailure::None;
}
} // namespace c3native
