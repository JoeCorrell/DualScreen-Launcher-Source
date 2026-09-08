#include "NativeWorkGate.h"
#include "State.h"
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
using namespace ac2ds;
int main() {
    native::WorkGate gate;
    assert(gate.Schedule());
    assert(!gate.Schedule() && !gate.BeginAction());
    std::promise<void> reading,finish;
    auto finished=finish.get_future();
    std::thread worker([&]{
        assert(gate.BeginRead());reading.set_value();finished.wait();gate.Complete();
    });
    reading.get_future().wait();
    // A stalled reader cannot block the caller, allow a native mutation or
    // accumulate a backlog of requests. No sleeps or timing assumptions.
    for(int i=0;i<100000;++i)assert(!gate.BeginAction() && !gate.Schedule());
    finish.set_value();worker.join();
    assert(gate.BeginAction());assert(!gate.Schedule() && !gate.BeginRead());gate.Complete();
    assert(gate.Schedule());gate.CancelPending();assert(!gate.BeginRead());

    // Contending producer/action threads share non-atomic adapter cache state
    // only under the gate. Every accepted sample is processed exactly once.
    std::atomic<bool> done{};int cache{};unsigned sampled{};
    std::thread reader([&]{
        while(!done.load()){
            if(gate.BeginRead()){++cache;++sampled;gate.Complete();}
            else std::this_thread::yield();
        }
    });
    unsigned actions{};
    for(int i=0;i<50000;++i){
        if(gate.BeginAction()){++cache;++actions;gate.Complete();}
        gate.Schedule();
    }
    // Drain the last request and obtain exclusive ownership before stopping.
    while(!gate.BeginAction())std::this_thread::yield();
    done.store(true);reader.join();assert(cache==int(actions+sampled));gate.Complete();

    StateStore store;Snapshot s;s.generation=1;s.revision=1;s.sampledAt=100;
    s.phase=Phase::Playing;s.capabilities=Inventory;
    Item item;item.id=9;item.revision=7;item.owned=true;item.actions=ActionBit(ActionKind::Equip);s.items.push_back(item);
    assert(store.Publish(s));Command c{0,1,1,9,7,0,ActionKind::Equip};
    assert(store.Queue(c,101));auto request=store.TryTakeForGame(102);
    assert(request && request->displayed->revision==request->command.revision);
    assert(!store.TryTakeForGame(103));
    assert(store.Queue(c,104));++s.revision;assert(store.Publish(s));
    assert(!store.TryTakeForGame(105)); // stale request cannot cross publication
    c.revision=2;assert(store.Queue(c,106));s.generation=2;s.revision=1;s.phase=Phase::Loading;
    assert(store.Publish(s));assert(!store.TryTakeForGame(107));
    assert(request->displayed->items.size()==1); // independent immutable lifetime
    std::cout<<"Native worker: no blocking/backlog, exclusive actions, concurrent cache ownership and stale commands passed\n";
}
