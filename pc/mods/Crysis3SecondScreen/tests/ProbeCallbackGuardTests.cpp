#include "../src/ProbeCallbackGuard.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
using c3probe::ProbeCallbackGuard;
static_assert(!std::is_copy_constructible<ProbeCallbackGuard::SystemScope>::value);
static_assert(!std::is_move_constructible<ProbeCallbackGuard::SystemScope>::value);
static_assert(!std::is_copy_constructible<ProbeCallbackGuard::UpdateScope>::value);
static_assert(!std::is_move_constructible<ProbeCallbackGuard::UpdateScope>::value);
int main() {
    {
        ProbeCallbackGuard g; auto update=g.EnterUpdate(41);
        assert(update.Outermost() && update.CanObserve() && update.EntryObservationEpoch()==1);
        assert(g.UpdateDepth()==1 && g.ObservedUpdateThread()==41);
    }
    {
        ProbeCallbackGuard g;
        { auto outer=g.EnterUpdate(41);
          { auto inner=g.EnterUpdate(41); assert(!inner.Outermost() && !inner.CanObserve()); assert(!outer.CanObserve()); }
          assert(outer.CanObserve()); }
        assert(g.UpdateDepth()==0);
    }
    {
        ProbeCallbackGuard g;
        auto update=g.EnterUpdate(41);
        { auto event=g.EnterSystemEvent(); assert(event.EntryObservationEpoch()==2); assert(!update.CanObserve()); }
        assert(g.SystemDepth()==0 && !update.CanObserve()); // epoch changed, even after event exits
    }
    {
        ProbeCallbackGuard g; auto event=g.EnterSystemEvent(); auto update=g.EnterUpdate(41);
        assert(!update.CanObserve());
    }
    {
        ProbeCallbackGuard g; auto outer=g.EnterSystemEvent();
        const auto first=outer.EntryObservationEpoch();
        { auto inner=g.EnterSystemEvent(); assert(inner.EntryObservationEpoch()==first+1 && g.SystemDepth()==2); }
        assert(g.SystemDepth()==1 && g.ObservationEpoch()==first+1);
        assert(outer.EntryObservationEpoch()==first); // immutable entry epoch, not latest nested epoch
    }
    {
        ProbeCallbackGuard g;
        try { auto event=g.EnterSystemEvent(); throw std::runtime_error("synthetic"); } catch(const std::runtime_error&) {}
        assert(g.SystemDepth()==0 && g.ObservationEpoch()==2);
        try { auto update=g.EnterUpdate(41); throw std::runtime_error("synthetic"); } catch(const std::runtime_error&) {}
        assert(g.UpdateDepth()==0);
    }
    {
        ProbeCallbackGuard g;
        { auto event=g.EnterSystemEvent(false); auto update=g.EnterUpdate(41,false);
          assert(!event.Tracked() && !event.EntryObservationEpoch() && !update.CanObserve()); }
        assert(g.ObservationEpoch()==1 && !g.SystemDepth() && !g.UpdateDepth() && !g.ObservedUpdateThread());
    }
    {
        ProbeCallbackGuard g;
        { auto first=g.EnterUpdate(41); assert(first.CanObserve()); }
        { auto second=g.EnterUpdate(42); assert(!second.CanObserve() && g.ThreadConflict()); }
        { auto original=g.EnterUpdate(41); assert(!original.CanObserve()); } // no automatic re-attestation
    }
    {
        ProbeCallbackGuard g; auto update=g.EnterUpdate(0);
        assert(!update.CanObserve() && g.ThreadConflict());
    }
    {
        ProbeCallbackGuard g(std::numeric_limits<uint32_t>::max());
        { auto event=g.EnterSystemEvent(); assert(event.EntryObservationEpoch()==0 && g.Exhausted()); }
        { auto event=g.EnterSystemEvent(); assert(event.EntryObservationEpoch()==1 && g.Exhausted()); }
        auto update=g.EnterUpdate(41); assert(!update.CanObserve()); // wrapped epochs stay disabled
    }
    {
        ProbeCallbackGuard g(0); auto update=g.EnterUpdate(41); assert(!update.CanObserve() && g.Exhausted());
    }
    {
        ProbeCallbackGuard g; auto outer=g.EnterSystemEvent(); const auto entry=outer.EntryObservationEpoch();
        std::thread concurrent([&] { for(unsigned i=0;i<10000;++i) { auto event=g.EnterSystemEvent(); assert(event.Tracked()); } });
        concurrent.join(); assert(g.ObservationEpoch()==entry+10000 && g.SystemDepth()==1);
        assert(outer.EntryObservationEpoch()==entry);
    }
    {
        ProbeCallbackGuard g; auto update=g.EnterUpdate(41);
        std::thread concurrent([&] { auto other=g.EnterUpdate(42); assert(!other.CanObserve()); });
        concurrent.join(); assert(g.ThreadConflict() && !update.CanObserve() && g.UpdateDepth()==1);
    }
    std::puts("Probe callback guards: 13 synthetic cases passed; no lifecycle-generation or game-thread attestation.");
}
