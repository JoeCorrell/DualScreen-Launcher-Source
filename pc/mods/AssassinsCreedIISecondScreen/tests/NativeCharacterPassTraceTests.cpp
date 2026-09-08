#include "NativeCharacterPassTrace.h"
#include <cassert>
#include <cstdio>
using namespace ac2ds::character_trace;
unsigned forwarded{};
void Quiet(const char*,...){}
void __fastcall Forward(void* object,void*,uint32_t pass,uint32_t instance){
    assert(object==reinterpret_cast<void*>(0x123400) && pass==0x456700 && instance==0x789a00);++forwarded;
}
int main(){
    skinned=Forward;alternate=Forward;
    auto invoke=[] {Skinned(reinterpret_cast<void*>(0x123400),nullptr,0x456700,0x789a00);Alternate(reinterpret_cast<void*>(0x123400),nullptr,0x456700,0x789a00);};
    invoke();assert(forwarded==2 && count==0);
    selected[0]={0x123400,0x789a00};selectedCount=1;armed=true;
    for(unsigned i=0;i<70;++i)invoke();armed=false;
    assert(forwarded==142 && count==140 && inFlight==0);
    assert(observedCalls==140);
    for(const auto& r:records)assert(r.renderer==0x123400 && r.expectedInstance==0x789a00 && r.argumentInstance==0x789a00);
    // Slot exchange verifies the predecessor and restores read-only protection.
    auto memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(memory);
    auto slot=static_cast<void* volatile*>(memory);*slot=reinterpret_cast<void*>(Forward);
    DWORD old{};assert(VirtualProtect(memory,4096,PAGE_READONLY,&old));
    assert(!Replace(slot,reinterpret_cast<void*>(Alternate),reinterpret_cast<void*>(Skinned)));
    assert(*slot==reinterpret_cast<void*>(Forward));
    assert(Replace(slot,reinterpret_cast<void*>(Forward),reinterpret_cast<void*>(Skinned)));
    MEMORY_BASIC_INFORMATION info{};assert(VirtualQuery(memory,&info,sizeof(info)) && info.Protect==PAGE_READONLY);
    assert(Replace(slot,reinterpret_cast<void*>(Skinned),reinterpret_cast<void*>(Forward)));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    installed=true;finished=false;armed=true;deadline=10;count=0;windows=1;attempts=1;
    Tick(10,Quiet);assert(!finished && !armed && deadline==0 && selectedCount==0);
    deadline=20;windows=3;Tick(20,Quiet);assert(finished && !armed);
    std::puts("Character trace: native thiscall forwarding, bounded capture, disabled fast path and atomic verified slot protection passed");
}
