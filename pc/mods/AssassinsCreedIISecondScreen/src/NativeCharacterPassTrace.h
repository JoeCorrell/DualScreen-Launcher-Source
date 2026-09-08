#pragma once
#include "NativeCharacterSource.h"
#include <windows.h>
#include <atomic>
#include <array>

namespace ac2ds::character_trace {
using Logger=void(*)(const char*,...);
// Native methods consume ECX and two stack arguments (ret8). The unused
// fastcall EDX slot expresses that ABI without GCC's non-member thiscall warning.
using Draw=void(__fastcall*)(void*,void*,uint32_t,uint32_t);
struct Local final:native::Memory {
    bool Copy(uint32_t address,void* output,size_t bytes)const override {
        SIZE_T got{};return address>=0x10000 && bytes<=65536 && address<=UINT32_MAX-bytes &&
            ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,bytes,&got) && got==bytes;
    }
};
struct Selection {uint32_t renderer{},instance{};};
struct Record {uint32_t renderer{},expectedInstance{},argumentInstance{},pass{},mode{},flags{},callback{},context{},device{},queued{},thread{},kind{};bool valid{};};
inline std::array<Selection,64> selected{};
inline std::array<Record,128> records{};
inline unsigned selectedCount{};
inline std::atomic<bool> armed{};
inline std::atomic<unsigned> inFlight{},count{};
inline bool installed{},finished{};
inline uint64_t lastTry{},deadline{},playerSince{};
inline uint32_t observedPlayer{};
inline unsigned attempts{},windows{};
inline std::atomic<unsigned> observedCalls{};
inline Draw skinned=reinterpret_cast<Draw>(0x00c06880);
inline Draw alternate=reinterpret_cast<Draw>(0x00b37b00);

// Only metadata is observed; no render calls, state blocks or engine mutations.
// The fast path after the one bounded capture is one atomic flag check.
inline void Observe(void* object,uint32_t pass,uint32_t instance,unsigned kind) noexcept {
    if(!armed.load(std::memory_order_acquire))return;
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    struct Leave {~Leave(){inFlight.fetch_sub(1,std::memory_order_release);}} leave;
    if(!armed.load(std::memory_order_acquire))return;
    observedCalls.fetch_add(1,std::memory_order_relaxed);
    uint32_t renderer=uint32_t(reinterpret_cast<uintptr_t>(object));
    const Selection* match=nullptr;
    for(unsigned i=0;i<selectedCount;++i)if(selected[i].renderer==renderer){match=&selected[i];break;}
    if(!match)return;
    unsigned index=count.fetch_add(1,std::memory_order_relaxed);if(index>=records.size())return;
    auto& r=records[index];r.renderer=renderer;r.expectedInstance=match->instance;r.argumentInstance=instance;
    r.pass=pass;r.kind=kind;r.thread=GetCurrentThreadId();Local memory;uint32_t handle{};
    r.valid=memory.Field(pass,4,handle) && handle>=0x10004 &&
        memory.Field(pass,0x14,r.mode) && memory.Field(pass,0x1c,r.flags) && memory.Field(pass,0x360,r.callback);
    if(r.valid){r.context=handle-4;r.valid=memory.Field(r.context,4,r.queued) && memory.Field(r.context,0xc,r.device);r.queued&=255;}
}
inline void __fastcall Skinned(void* object,void*,uint32_t pass,uint32_t instance){Observe(object,pass,instance,0);skinned(object,nullptr,pass,instance);}
inline void __fastcall Alternate(void* object,void*,uint32_t pass,uint32_t instance){Observe(object,pass,instance,1);alternate(object,nullptr,pass,instance);}
inline bool Replace(void* volatile* slot,void* expected,void* replacement){
    DWORD protection{};if(!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&protection))return false;
    bool changed=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    DWORD ignored{};VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&ignored);return changed;
}
// Called only after Runtime's full executable hash gate. No executable bytes
// are rewritten. Aligned, verified vtable slots are exchanged atomically.
inline bool Install(){
    if(installed)return true;Local memory;uint32_t a{},b{};
    if(!memory.Read(0x01dcfbb8,a) || a!=0x00c06880 || !memory.Read(0x01db684c,b) || b!=0x00b37b00)return false;
    auto slotA=reinterpret_cast<void* volatile*>(0x01dcfbb8),slotB=reinterpret_cast<void* volatile*>(0x01db684c);
    if(!Replace(slotA,reinterpret_cast<void*>(skinned),reinterpret_cast<void*>(Skinned)))return false;
    if(!Replace(slotB,reinterpret_cast<void*>(alternate),reinterpret_cast<void*>(Alternate))){
        Replace(slotA,reinterpret_cast<void*>(Skinned),reinterpret_cast<void*>(skinned));return false;
    }
    installed=true;return true;
}
// Panel thread only. At most three two-second windows and ten graph reads.
// Wait for a stable player so loading-screen frames do not consume the capture.
// Thread callbacks never log or allocate. The observer does not publish Preview.
inline void Tick(uint64_t now,Logger log){
    if(!installed || finished)return;
    if(deadline){
        if(now<deadline)return;armed.store(false,std::memory_order_release);
        if(inFlight.load(std::memory_order_acquire))return;
        unsigned available=std::min<unsigned>(count.load(),records.size());
        log("character-pass capture records=%u selected=%u truncated=%d calls=%u window=%u",available,selectedCount,int(count.load()>records.size()),observedCalls.load(),windows);
        for(unsigned i=0;i<available;++i){const auto& r=records[i];
            log("character-pass kind=%lu renderer=%08lx expected=%08lx argument=%08lx pass=%08lx mode=%lu flags=%08lx override=%08lx context=%08lx queued=%lu device=%08lx thread=%lu valid=%d",
                (unsigned long)r.kind,(unsigned long)r.renderer,(unsigned long)r.expectedInstance,(unsigned long)r.argumentInstance,
                (unsigned long)r.pass,(unsigned long)r.mode,(unsigned long)r.flags,(unsigned long)r.callback,
                (unsigned long)r.context,(unsigned long)r.queued,(unsigned long)r.device,(unsigned long)r.thread,int(r.valid));
        }
        if(!available && windows<3 && attempts<10){deadline=0;lastTry=now;selectedCount=0;return;}
        finished=true;return;
    }
    if(now-lastTry<2000)return;lastTry=now;Local memory;native::Roots roots{};
    if(!native::ReadRoots(memory,roots) || !roots.player){observedPlayer=0;playerSince=0;return;}
    if(observedPlayer!=roots.player){observedPlayer=roots.player;playerSince=now;return;}
    if(now-playerSince<5000)return;
    if(attempts>=10){finished=true;log("character-pass capture stopped: no coherent character selection");return;}
    ++attempts;
    auto source=native::ReadCharacterSource(memory,roots);
    if(!source || source->meshes.empty() || source->meshes.size()>selected.size())return;
    selectedCount=0;
    for(const auto& mesh:source->meshes)if(mesh.renderer)selected[selectedCount++]={mesh.renderer,mesh.instance};
    if(!selectedCount)return;
    ++windows;deadline=now+2000;count=0;observedCalls=0;armed.store(true,std::memory_order_release);
    log("character-pass bounded metadata capture armed; no D3D calls or preview publication");
}
}
