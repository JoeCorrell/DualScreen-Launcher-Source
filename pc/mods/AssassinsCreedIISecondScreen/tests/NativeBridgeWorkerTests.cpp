// Exercise the actual bridge in this headless test process. No import hook is
// installed and no game executable or game function is loaded/called.
#include "NativeBridge.cpp"
#include <cassert>
#include <iostream>
namespace {
std::atomic<DWORD> sampleThread{},actionThread{};
void TestLog(const char* format,...) {
    if(std::strstr(format,"native generation="))sampleThread.store(GetCurrentThreadId());
    if(std::strstr(format,"native cape action request="))actionThread.store(GetCurrentThreadId());
}
}
int main(){
    using namespace ac2ds;
    StateStore localStore;destination=&localStore;logNative=TestLog;
    sampleEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(sampleEvent);
    // Reserve a queued sample without waking its worker. The real game-pump
    // service must return while a background sample is arbitrarily delayed.
    assert(workGate.Schedule());
    for(unsigned i=0;i<100000;++i)ServicePump();
    assert(localStore.Read()->revision==0 && ownerThread.load()==GetCurrentThreadId());
    HANDLE worker=CreateThread(nullptr,0,ReadWorker,nullptr,0,nullptr);assert(worker);
    assert(SetEvent(sampleEvent));
    const auto deadline=GetTickCount64()+5000;
    while(!sampleThread.load() && GetTickCount64()<deadline)Sleep(1);
    assert(sampleThread.load() && sampleThread.load()!=GetCurrentThreadId());
    // Wait from this test harness only; production game-thread acquisition is
    // a single try. Hold the adapter gate while preparing an immutable request.
    bool acquired=false;
    while(!(acquired=workGate.BeginAction()) && GetTickCount64()<deadline)Sleep(1);
    assert(acquired && localStore.Read()->revision>0);
    Snapshot s;s.generation=generation+1;s.revision=1;s.sampledAt=GetTickCount64();
    s.phase=Phase::Playing;s.capabilities=Inventory;
    Item cape;cape.id=(uint64_t(1)<<32)|2;cape.revision=1;cape.owned=true;
    cape.actions=ActionBit(ActionKind::Equip);s.items.push_back(cape);
    assert(localStore.Publish(s));
    assert(localStore.Queue({0,s.generation,1,cape.id,1,0,ActionKind::Equip},s.sampledAt));
    workGate.Complete();
    // capeCallsReady stays false: this tests the dispatch boundary without
    // ever calling a hard-coded game address or modifying any game memory.
    ServicePump();assert(actionThread.load()==GetCurrentThreadId());
    stopWorker.store(true);SetEvent(sampleEvent);
    assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
    CloseHandle(worker);CloseHandle(sampleEvent);sampleEvent=nullptr;destination=nullptr;
    std::cout<<"Actual native bridge: delayed worker never blocks pump, samples run off-thread, commands stay on game-thread boundary\n";
}
