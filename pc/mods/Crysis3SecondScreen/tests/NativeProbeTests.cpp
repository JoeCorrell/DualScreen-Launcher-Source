// Include the implementation to exercise its actual forwarding boundaries.
// No game process, slot installation, input export, or worker is invoked.
#include "../src/NativeProbe.cpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
uint32_t calls{},seen0{},seen1{},seen2{}; void* seenThis{};
uint32_t __attribute__((fastcall)) FakeUpdate(void* receiver,void*,uint32_t arg0,uint32_t arg1) {
    ++calls; seenThis=receiver; seen0=arg0; seen1=arg1; return 0xdeadbeefu;
}
void __attribute__((fastcall)) FakeSystem(void* receiver,void*,uint32_t event,uint32_t arg0,uint32_t arg1) {
    ++calls; seenThis=receiver; seen0=event; seen1=arg0; seen2=arg1;
}
bool expectTrackedNesting{};
uint32_t __attribute__((fastcall)) NestedUpdate(void* receiver,void*,uint32_t arg0,uint32_t arg1) {
    using namespace c3probe;
    ++calls;
    assert(callbacks.UpdateDepth()==(expectTrackedNesting?(arg0==0?1u:2u):0u));
    if(!arg0) {
        const auto before=revision.load();
        assert(UpdateHook(receiver,nullptr,1,arg1)==0x11223344u);
        assert(revision.load()==before); // Inner hook never publishes a sample.
        assert(callbacks.UpdateDepth()==(expectTrackedNesting?1u:0u));
    }
    return 0x11223344u;
}
uint32_t __attribute__((fastcall)) ThrowingUpdate(void*,void*,uint32_t,uint32_t) {
    ++calls; throw std::runtime_error("synthetic original Update failure");
}
void __attribute__((fastcall)) ThrowingSystem(void*,void*,uint32_t,uint32_t,uint32_t) {
    ++calls; throw std::runtime_error("synthetic original system failure");
}
void __attribute__((fastcall)) NestedSystem(void* receiver,void*,uint32_t event,uint32_t arg0,uint32_t arg1) {
    using namespace c3probe;
    ++calls;
    assert(callbacks.SystemDepth()==(event==101?1u:2u));
    if(event==101) {
        SystemHook(receiver,nullptr,102,arg0,arg1);
        assert(callbacks.SystemDepth()==1);
    }
}
uint32_t __attribute__((fastcall)) UpdateWithSystemEvent(void* receiver,void*,uint32_t arg0,uint32_t arg1) {
    ++calls;
    c3probe::SystemHook(static_cast<uint8_t*>(receiver)+0x10,nullptr,77,arg0,arg1);
    return 0xaabbccddu;
}
struct SyntheticRoot {
    void* globalPage{};
    void* root{};
    SyntheticRoot() {
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        const auto granularity=uintptr_t(system.dwAllocationGranularity);
        const auto base=(c3probe::RootSlot/granularity)*granularity;
        globalPage=VirtualAlloc(reinterpret_cast<void*>(base),granularity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        assert(globalPage==reinterpret_cast<void*>(base));
        root=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); assert(root);
        *reinterpret_cast<uint32_t*>(c3probe::RootSlot)=uint32_t(reinterpret_cast<uintptr_t>(root));
        // Intentionally not an engine vtable. The exact root-identity check can
        // pass, but actor/phase readers must reject this synthetic object.
    }
    ~SyntheticRoot() {
        if(root) VirtualFree(root,0,MEM_RELEASE);
        if(globalPage) VirtualFree(globalPage,0,MEM_RELEASE);
    }
};
}
int main() {
    using namespace c3probe;
    originalUpdate.store(FakeUpdate); originalSystem.store(FakeSystem);
    active.store(false); stopping.store(false);
    void* receiver=reinterpret_cast<void*>(0x12345678u);
    const auto beforeEpoch=callbacks.ObservationEpoch();
    for(uint32_t i=0;i<10000;++i) {
        const uint32_t result=UpdateHook(receiver,nullptr,i,0xffffffffu-i);
        assert(result==0xdeadbeefu && seenThis==receiver && seen0==i && seen1==0xffffffffu-i);
        SystemHook(receiver,nullptr,i,0x87654321u,0xaabbccddu);
        assert(seenThis==receiver && seen0==i && seen1==0x87654321u && seen2==0xaabbccddu);
    }
    assert(calls==20000 && callbacks.ObservationEpoch()==beforeEpoch && callbacks.SystemDepth()==0 && callbacks.UpdateDepth()==0 && revision.load()==0);
    active.store(true); stopping.store(true); // Disabled/exit path must still forward exactly once.
    assert(UpdateHook(receiver,nullptr,0,1)==0xdeadbeefu);
    SystemHook(receiver,nullptr,27,2,3);
    assert(calls==20002 && callbacks.ObservationEpoch()==beforeEpoch && revision.load()==0);
    std::array<uint8_t,3> abc{{'a','b','c'}};
    Digest digest; assert(digest.Add(abc.data(),3));
    assert(digest.Finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(MemoryHash(reinterpret_cast<uintptr_t>(abc.data()),3)==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    uint32_t value{};
    assert(!Read(0,value)); assert(!ReadBytes(0xfffffff0u,&value,32));
    assert(MemoryHash(0,0).empty()); assert(!ProfileReady());
    SystemJournal journal;
    for(uint32_t i=0;i<130;++i) journal.Push({i,i+1,7,i,9,10});
    std::array<SystemRecord,128> events{}; uint32_t dropped{};
    assert(journal.Drain(events,dropped)==128 && dropped==2);
    for(uint32_t i=0;i<128;++i) assert(events[i].event==i && events[i].epoch==i+1);
    assert(journal.Drain(events,dropped)==0 && dropped==0);

    // All following mappings and callbacks are confined to THIS standalone test
    // process. No game is accessed, no slots are installed, and no worker starts.
    active.store(false); stopping.store(false); originalUpdate.store(NestedUpdate);
    expectTrackedNesting=false;
    assert(UpdateHook(receiver,nullptr,0,9)==0x11223344u);
    assert(callbacks.UpdateDepth()==0 && revision.load()==0);
    SyntheticRoot synthetic;
    void* root=synthetic.root;
    void* systemReceiver=static_cast<uint8_t*>(root)+0x10;
    active.store(true);
    originalUpdate.store(ThrowingUpdate);
    bool caught=false;
    try { UpdateHook(root,nullptr,1,2); } catch(const std::runtime_error&) { caught=true; }
    assert(caught && callbacks.UpdateDepth()==0 && revision.load()==0);
    originalSystem.store(ThrowingSystem);
    const auto exceptionEpoch=callbacks.ObservationEpoch(); caught=false;
    try { SystemHook(systemReceiver,nullptr,27,3,4); } catch(const std::runtime_error&) { caught=true; }
    assert(caught && callbacks.SystemDepth()==0 && callbacks.ObservationEpoch()==exceptionEpoch+1);
    assert(systemJournal.Drain(events,dropped)==0); // No fabricated successful completion.

    originalUpdate.store(NestedUpdate); expectTrackedNesting=true;
    assert(UpdateHook(root,nullptr,0,5)==0x11223344u);
    assert(callbacks.UpdateDepth()==0 && revision.load()==1);
    assert(!pending.consistent && pending.actorReadFailure==uint32_t(c3native::ActorReadFailure::UnsupportedLayout));
    assert(pending.phaseReadFailure==uint32_t(c3native::ActorReadFailure::UnsupportedLayout));
    assert(!pending.phase.pauseKnown && !pending.phase.cutsceneKnown && !pending.entity && !pending.health);

    originalSystem.store(NestedSystem);
    const auto nestedEpoch=callbacks.ObservationEpoch();
    SystemHook(systemReceiver,nullptr,101,0x1234,0x5678);
    assert(callbacks.SystemDepth()==0 && callbacks.ObservationEpoch()==nestedEpoch+2);
    assert(systemJournal.Drain(events,dropped)==2 && !dropped);
    assert(events[0].event==102 && events[0].epoch==nestedEpoch+2);
    assert(events[1].event==101 && events[1].epoch==nestedEpoch+1);
    assert(events[0].arg0==0x1234 && events[1].arg1==0x5678);

    originalSystem.store(FakeSystem); originalUpdate.store(UpdateWithSystemEvent);
    const auto beforeEventUpdate=revision.load();
    assert(UpdateHook(root,nullptr,12,13)==0xaabbccddu);
    assert(revision.load()==beforeEventUpdate && callbacks.UpdateDepth()==0 && callbacks.SystemDepth()==0);
    assert(systemJournal.Drain(events,dropped)==1 && events[0].event==77);
    const auto outsideScope=Observe(reinterpret_cast<uintptr_t>(root),GetTickCount64());
    assert(!outsideScope.consistent && !outsideScope.phase.pauseKnown);
    assert(outsideScope.actorReadFailure==uint32_t(c3native::ActorReadFailure::GuardUnverified));
    assert(outsideScope.phaseReadFailure==uint32_t(c3native::ActorReadFailure::GuardUnverified));

    originalUpdate.store(FakeUpdate);
    const auto conflictRevision=revision.load();
    std::thread other([&] { assert(UpdateHook(root,nullptr,14,15)==0xdeadbeefu); });
    other.join();
    assert(callbacks.ThreadConflict() && callbacks.UpdateDepth()==0 && revision.load()==conflictRevision);
    assert(UpdateHook(root,nullptr,16,17)==0xdeadbeefu && revision.load()==conflictRevision);
    active.store(false);
    std::cout<<"PASS: native probe DWORD/return forwarding, disabled/nested/exception paths, entry epochs, read guards, thread conflict, bounded reads and profile rejection\n";
}
