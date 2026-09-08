// Guarded native callback RESEARCH, not the finished second-screen payload.
// No native equipment calls, UI input, code trampolines, or remote injection.
#include "ProbeSupport.h"
#include "ProbeJournal.h"
#include "NativeActorRead.h"
#include "ProbeCallbackGuard.h"
#include "ReadOnlyTrial.h"
#include "NativeWeaponProfile.h"
#include "NativeSuitProfile.h"
#include "NativeObjectiveRead.h"
#include "NativeObjectiveProfile.h"
#include "NativeStatsRead.h"
#ifdef C3_NATIVE_UI
#include "Panel.h"
#endif
#include <dinput.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace c3probe {
static_assert(sizeof(void*)==4,"Observed game ABI is i386 only");
constexpr uintptr_t Image=0x37000000, RootSlot=0x3b9fde30;
constexpr uintptr_t UpdateSlot=0x399befa4, UpdateAddress=0x38189340;
constexpr uintptr_t SystemSlot=0x399bef38, SystemAddress=0x3818a2c0;
constexpr char SourceHash[]="fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055";
// ECX receiver + two/three opaque DWORD stack arguments. Verified RET 8/RET 12.
// Do not adopt the decompiler's inferred float/bool argument types.
using UpdateFn=uint32_t(__attribute__((fastcall)) *)(void*,void*,uint32_t,uint32_t);
using SystemFn=void(__attribute__((fastcall)) *)(void*,void*,uint32_t,uint32_t,uint32_t);
std::atomic<UpdateFn> originalUpdate{};
std::atomic<SystemFn> originalSystem{};
HMODULE self{},systemInput{};
INIT_ONCE originalOnce=INIT_ONCE_STATIC_INIT;
std::atomic<bool> started{},stopping{},active{};
std::atomic<bool> weaponProfileVerified{},weaponNameProfileVerified{},suitProfileVerified{},objectiveProfileVerified{};
std::atomic<bool> statsProfileVerified{};
std::atomic<bool> attachmentProfileVerified{};
std::atomic<uint32_t> trialPublishFlags{},trialPublishPhase{},trialCaptureMs{};
c3::Mailbox panelMailbox;
uint64_t panelSequence{}; // Only the accepted, non-reentrant callback writes it.
c3native::NativeObjectiveScratch objectiveScratch; // Large copied buffers, never native callback stack.
c3native::NativeObjectiveValues objectiveValues;
void Log(const char* line) noexcept;
#ifdef C3_NATIVE_UI
wchar_t panelAssets[32768]{};
DWORD WINAPI PanelWorker(void*) noexcept {
    try {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        c3::RunPanel(panelMailbox,panelAssets,stopping,Log);
    } catch(...) { Log("[panel] stopped after internal error; gameplay left untouched"); }
    return 0;
}
#endif
ProbeCallbackGuard callbacks;
std::atomic<uint32_t> lastSystemEvent{},lastSystemThread{},wrongThreadCalls{};
std::atomic<uint32_t> revision{};
SRWLOCK frameLock=SRWLOCK_INIT;
SRWLOCK logLock=SRWLOCK_INIT;
wchar_t logPath[32768]{};
struct Observation {
    uint64_t at{};
    uint32_t epoch{},thread{},actor{},entity{},mode{},channel{},systemEvent{},systemThread{},wrongThreads{};
    float health{},maxHealth{},energy{},candidateMaxEnergy{};
    bool consistent{};
    uint32_t actorReadFailure{},phaseReadFailure{};
    c3native::NativePhaseValues phase{};
};
Observation pending{};
SystemJournal systemJournal;

void Log(const char* line) noexcept {
    if(!logPath[0]) return;
    if(!TryAcquireSRWLockExclusive(&logLock)) return;
    struct UnlockLog { ~UnlockLog() { ReleaseSRWLockExclusive(&logLock); } } unlock;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(GetFileAttributesExW(logPath,GetFileExInfoStandard,&info) && (info.nFileSizeHigh || info.nFileSizeLow>1024*1024)) {
        wchar_t previous[32768]{};
        if(std::wcslen(logPath)+10>=32768) return;
        std::swprintf(previous,32768,L"%ls.previous",logPath);
        if(!MoveFileExW(logPath,previous,MOVEFILE_REPLACE_EXISTING)) return;
    }
    FILE* file=_wfopen(logPath,L"ab");
    if(file) { std::fprintf(file,"[%llu] %s\n",static_cast<unsigned long long>(GetTickCount64()),line); std::fclose(file); }
}
bool CurrentRoot(uintptr_t receiver,uint32_t adjustment=0) noexcept {
    uint32_t root{};
    return Read(RootSlot,root) && root && uint64_t(root)+adjustment==receiver;
}
void PublishTrial(uint32_t root,uint64_t now,const ProbeCallbackGuard::UpdateScope& scope) {
    if(!scope.CanObserve()) return;
    auto read=[](uint32_t address,void* bytes,size_t size) { return ReadBytes(address,bytes,size); };
    auto guard=[] {
        return c3native::ActorFrameGuard{callbacks.ObservationEpoch(),callbacks.ObservedUpdateThread(),GetCurrentThreadId(),
            callbacks.SystemDepth(),active.load(),active.load() && !callbacks.ThreadConflict() &&
            !callbacks.Exhausted() && callbacks.UpdateDepth()==1 && callbacks.ObservedUpdateThread()==GetCurrentThreadId()};
    };
    c3native::actor_detail::RawActorFrame identity{},after{};
    c3native::actor_detail::RawPhaseFrame phaseIdentity{},phaseAfter{};
    c3native::NativeActorValues actor{}; c3native::NativePhaseValues phase{};
    const auto observationEpoch=callbacks.ObservationEpoch();
    bool valid=c3native::actor_detail::Once(read,root,identity)==c3native::ActorReadFailure::None &&
        c3native::actor_detail::PhaseOnce(read,root,phaseIdentity)==c3native::ActorReadFailure::None &&
        c3native::ReadCurrentActor(read,guard,root,actor)==c3native::ActorReadFailure::None &&
        c3native::ReadCurrentPhase(read,guard,root,phase)==c3native::ActorReadFailure::None;
    for(size_t i=0;i<3;++i) valid=valid && identity.identity[i]==phaseIdentity.identity[i];
    static c3::Snapshot snapshot;
    c3::TrialVitals(snapshot,actor,phase,valid,observationEpoch,++panelSequence,now);
    uint32_t publicationFlags=(valid?1u:0u) | (c3::Current(snapshot,now)?2u:0u);
    const auto requestedPage=panelMailbox.RequestedPage();
    if(valid && c3::Current(snapshot,now)) {
        const uint32_t actorAddress=identity.identity[7],framework=identity.identity[1];
        if(weaponProfileVerified.load() && requestedPage==c3::Page::Weapons) {
          uint32_t inventoryAddress{},system{},frameworkSystem{},item{};
          c3::research::InventoryObservation inventory{};
          c3::research::RegularSingleObservation weapon{};
          if(Read(actorAddress+0x40,inventoryAddress) &&
           c3::research::ReadInventory(read,inventoryAddress,actor.entity,inventory) &&
           Read(0x3b9f9c68,system) && Read(framework+0x4b4,frameworkSystem) &&
           uint64_t(frameworkSystem)+4==system) {
            c3::research::OwnedWeaponList names{};
            if(requestedPage==c3::Page::Weapons && weaponNameProfileVerified.load() &&
               c3::research::ReadOwnedWeaponList(read,true,actorAddress,actor.entity,system,names) &&
               names.currentItem==inventory.currentItem) {
                c3::TrialOwnedWeapons(snapshot,names); publicationFlags|=0x10;
            }
            if(c3::research::ReadItemPointer(read,system,inventory.currentItem,item) &&
               c3::research::ReadRegularSingle(read,true,actorAddress,actor.entity,item,weapon)) {
                c3::TrialWeapon(snapshot,weapon); publicationFlags|=0x20;
                c3::research::AmmoObservation ammo{};
                if(c3::research::ReadInventoryAmmo(read,inventory,weapon.ammoClass,ammo))
                    c3::TrialInventoryAmmo(snapshot,weapon,ammo);
            }
            if(attachmentProfileVerified.load()) {
                // Only the two visible loadout weapons per sample; no background
                // archive work or engine calls in this callback.
                for(size_t i=0;i<std::min<size_t>(2,snapshot.weapons.size());++i) {
                    c3::research::NativeAttachmentValues attachments{};
                    if(c3::research::ReadOwnedAttachments(read,true,actorAddress,actor.entity,system,
                        uint32_t(snapshot.weapons.entries[i].key),attachments))
                        c3::TrialAttachments(snapshot,attachments);
                }
            }
            uint32_t inventoryAfter{},systemAfter{},frameworkSystemAfter{},itemAfter{};
            c3::research::InventoryObservation selectionAfter{};
            if(!Read(actorAddress+0x40,inventoryAfter) || inventoryAfter!=inventoryAddress ||
               !c3::research::ReadInventory(read,inventoryAfter,actor.entity,selectionAfter) ||
               selectionAfter.currentItem!=inventory.currentItem ||
               !Read(0x3b9f9c68,systemAfter) || systemAfter!=system ||
               !Read(framework+0x4b4,frameworkSystemAfter) || frameworkSystemAfter!=frameworkSystem ||
               (item && (!c3::research::ReadItemPointer(read,systemAfter,selectionAfter.currentItem,itemAfter) || itemAfter!=item))) {
                snapshot.weapons={}; snapshot.resources={};
                publicationFlags&=~0x30u;
            }
          }
        }
        if(actor.hasSuit && suitProfileVerified.load() && requestedPage==c3::Page::Suit) {
            c3native::NativeSuitModules modules{};
            if(c3native::ReadSuitModules(read,root,actorAddress,actor.entity,true,guard(),guard,modules)==c3native::SuitReadFailure::None) {
                c3::TrialSuitModules(snapshot,modules); publicationFlags|=0x40;
            }
        }
        if(statsProfileVerified.load() && requestedPage==c3::Page::Status) {
            if(c3native::ReadStatistics(read,root,actorAddress,actor.entity,true,guard(),guard,snapshot.statistics)==c3native::StatsReadFailure::None)
                publicationFlags|=0x100;
        }
        if(objectiveProfileVerified.load() && (requestedPage==c3::Page::Objectives || requestedPage==c3::Page::Map)) {
            if(c3native::ReadCurrentObjectives(read,guard,true,true,root,objectiveScratch,objectiveValues)==c3native::ObjectiveReadFailure::None) {
                snapshot.mission=objectiveValues.missionTitle; snapshot.location=objectiveValues.location;
                snapshot.objectives=objectiveValues.objectives; publicationFlags|=0x80;
            }
            // Scratch stores raw copied identities only during this callback.
            objectiveScratch={}; objectiveValues={};
        }
    }
    // No native pointers survive this function. All fields share a checked
    // actor/context and phase identity spanning the optional weapon reads.
    valid=valid && c3native::actor_detail::Once(read,root,after)==c3native::ActorReadFailure::None && identity.Same(after) &&
        c3native::actor_detail::PhaseOnce(read,root,phaseAfter)==c3native::ActorReadFailure::None && phaseIdentity.Same(phaseAfter) &&
        observationEpoch==callbacks.ObservationEpoch() && scope.CanObserve();
    if(!valid) c3::TrialVitals(snapshot,{}, {},false,callbacks.ObservationEpoch(),panelSequence,now);
    if(valid) publicationFlags|=4;
    if(panelMailbox.Publish(snapshot)) publicationFlags|=8;
    trialPublishPhase.store(uint32_t(snapshot.phase));
    trialCaptureMs.store(uint32_t(GetTickCount64()-now));
    trialPublishFlags.store(publicationFlags);
}
Observation Observe(uintptr_t root,uint64_t now) noexcept {
    Observation out{}; out.at=now; out.epoch=callbacks.ObservationEpoch();
    out.thread=GetCurrentThreadId(); out.systemEvent=lastSystemEvent.load();
    out.systemThread=lastSystemThread.load(); out.wrongThreads=wrongThreadCalls.load();
    // Diagnostic observation scope only. Double reads validate the authoritative
    // normal-client path, not just the cache. Still no production Snapshot or
    // action authorization: full callback/transition attestation is outstanding.
    auto read=[](uint32_t address,void* bytes,size_t size) { return ReadBytes(address,bytes,size); };
    auto guard=[] {
        return c3native::ActorFrameGuard{callbacks.ObservationEpoch(),callbacks.ObservedUpdateThread(),GetCurrentThreadId(),
            callbacks.SystemDepth(),active.load(),active.load() && !callbacks.ThreadConflict() &&
            !callbacks.Exhausted() && callbacks.UpdateDepth()==1 && callbacks.ObservedUpdateThread()==GetCurrentThreadId()};
    };
    c3native::NativeActorValues actor{};
    out.actorReadFailure=uint32_t(c3native::ReadCurrentActor(read,guard,uint32_t(root),actor));
    out.phaseReadFailure=uint32_t(c3native::ReadCurrentPhase(read,guard,uint32_t(root),out.phase));
    out.consistent=!out.actorReadFailure && !out.phaseReadFailure && out.epoch==callbacks.ObservationEpoch() && !callbacks.SystemDepth();
    if(out.consistent) {
        out.entity=actor.entity; out.health=actor.health; out.maxHealth=actor.maxHealth;
        out.energy=actor.energy; out.candidateMaxEnergy=actor.maxEnergy;
        out.mode=actor.mode; out.channel=actor.channel;
    } else out.phase={};
    return out;
}
uint32_t __attribute__((fastcall)) UpdateHook(void* receiver,void*,uint32_t arg0,uint32_t arg1) {
    const bool track=active.load(std::memory_order_acquire) && !stopping.load() &&
        CurrentRoot(reinterpret_cast<uintptr_t>(receiver));
    auto scope=callbacks.EnterUpdate(GetCurrentThreadId(),track);
    const uint32_t result=originalUpdate.load(std::memory_order_acquire)(receiver,nullptr,arg0,arg1);
    if(track && callbacks.ThreadConflict()) wrongThreadCalls.fetch_add(1);
    if(!active.load(std::memory_order_acquire) || stopping.load() || !scope.CanObserve() ||
        !CurrentRoot(reinterpret_cast<uintptr_t>(receiver))) return result;
    static uint64_t lastAt{};
    const uint64_t now=GetTickCount64();
    // Bounded 2.5 Hz telemetry, no off-page inventory/objective/module polling.
    if(now-lastAt<400) return result;
    lastAt=now;
    const Observation next=Observe(reinterpret_cast<uintptr_t>(receiver),now);
    if(!scope.CanObserve()) return result;
    PublishTrial(uint32_t(reinterpret_cast<uintptr_t>(receiver)),now,scope);
    if(TryAcquireSRWLockExclusive(&frameLock)) {
        pending=next; revision.fetch_add(1,std::memory_order_release); ReleaseSRWLockExclusive(&frameLock);
    }
    return result;
}
void __attribute__((fastcall)) SystemHook(void* receiver,void*,uint32_t event,uint32_t arg0,uint32_t arg1) {
    const bool track=active.load(std::memory_order_acquire) && !stopping.load() &&
        CurrentRoot(reinterpret_cast<uintptr_t>(receiver),0x10);
    auto scope=callbacks.EnterSystemEvent(track);
    if(track) {
        lastSystemEvent.store(event); lastSystemThread.store(GetCurrentThreadId());
    }
    originalSystem.load(std::memory_order_acquire)(receiver,nullptr,event,arg0,arg1);
    if(track) {
        systemJournal.Push({GetTickCount64(),scope.EntryObservationEpoch(),GetCurrentThreadId(),event,arg0,arg1});
    }
}
bool ActorProfileReady() {
    // Complete ACTOR-CONTRACT fingerprint table, including phase ownership.
    struct Fingerprint { uint32_t address,size; const char* hash; };
    constexpr Fingerprint ranges[]={
        {0x3802f430,0x4,"c8cc0b90c656bd6a43ec1c5684c1a494142d2fce4803153d9b1a0944cad7e281"},
        {0x3749e9b0,0xf,"cfcdd04833f49b359500f06df6acc58c27933c14719634a6d49e0f7acb24b86c"},
        {0x37486e90,0x94,"4fbcd8de3787b36f32f2db2909b79fa30547b0dd0381a1098c75a623d17b0def"},
        {0x385eeb80,0x4,"8a905797df24e4a1fe68a2f630971aa55b606cd6a92fed57303379d0f93a7a2d"},
        {0x3804d9c0,0x20,"89156db0cdac547ffa5e4bd1f91f7321c6918632331f16847386b2756344a9f8"},
        {0x370d8f00,0x10,"f7ad03246f1eb8d3f73103e6bc87a24fa7af348e86607db825535159a1ec976d"},
        {0x38168070,0x7e,"ca5fe84e3b9d7d6eb79724b8093a741104a1df2984b7d26e1ac21e25a4b3a027"},
        {0x381684f0,0x19a,"80fd90eba106d598cd87bee8d639126c6fe0e0e5f6377e86c550892401657e68"},
        {0x38167260,0x27,"734fa3e848b2e62a6348817f0ac1faed63f5aef11874be8b439e3c1e97267734"},
        {0x3816d2e0,0x83,"7f77c926a7a3a00ff01e5259026f7a3c91d3eb9244c9c797c9c4b01837bc21f9"},
        {0x3816e310,0x19f,"43099c92e6c28185de51219de6351d418b7360445c8f220f85b7b3d455aae364"},
        {0x3749eeb0,0x19,"b14f90471f53e0e132b0810b437b3fb68b16f2660a444d27aba72b975516054d"},
        {0x3749eed0,0x1f,"2347857d736193ff5707b7d6b4e9211ba6184735dd53af1eea7933b78bd39a75"},
        {0x3749dc40,0x1c,"617183159ccd53e407c73909bfb1388cd82ccda0d71c649c3759b5da22ea2824"},
        {0x3749c660,0x20,"53a78d712205162c655389baf6f1fca1a4300cd3f1ab6310fbda905a49003abb"},
        {0x37ce8800,0x4,"fcbf383155b3c946776d097d9104628a7375812784049a6842b7cb665ea1aef7"},
        {0x382bec00,0xf5,"447b87227828b119ba2888dda7c1ddf69d01b302a218dbeaabdb3f01e569cca0"},
        {0x382bed00,0xda,"056ee41d841da487b1cb45f7620941887d164982880e65ea82bee7e4fe226bfb"},
        {0x382bede0,0x6f,"e68426440b94086729871bfa2e52354d3b59a754e9b0a4c9517c7690dcc90a7b"},
        {0x382befd0,0x50,"8f5ffe3ae902fa496c1f23b73be97fafebdfadcdbb949991258961c929c409e6"},
        {0x3818b721,0x13,"ae98347b26fde25624db60a50b0e8d0bd1177b18710e95db8a61dca42ef2877e"},
    };
    for(const auto& range:ranges) if(MemoryHash(range.address,range.size)!=range.hash) return false;
    return true;
}
bool ProfileReady() {
    uint32_t root{},table{},systemTable{},update{},system{};
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))==Image &&
        MemoryHash(Image,1024)=="fb530657a66ea51a6a7495c36085e0c856d6daccffbacc1db8c037d48c55e25d" &&
        Read(RootSlot,root) && root && Read(root,table) && table==0x399bef80 &&
        Read(root+0x10,systemTable) && systemTable==SystemSlot &&
        Read(UpdateSlot,update) && update==UpdateAddress && Read(SystemSlot,system) && system==SystemAddress &&
        MemoryHash(UpdateAddress,0xb6e)=="31df8ffc4cb79f4d75c68ffd7baedb1a3c11bcb91ae29946e4ed080d20712e3d" &&
        MemoryHash(SystemAddress,0x510)=="fc8239c696e1eb0d2b71abeeb2f0115abfe3b681da501eb39119e63f54a2a01f" &&
        MemoryHash(0x38168070,0x7e)=="ca5fe84e3b9d7d6eb79724b8093a741104a1df2984b7d26e1ac21e25a4b3a027" &&
        MemoryHash(0x3804d9c0,0x20)=="89156db0cdac547ffa5e4bd1f91f7321c6918632331f16847386b2756344a9f8" &&
        MemoryHash(0x370d8f00,0x10)=="f7ad03246f1eb8d3f73103e6bc87a24fa7af348e86607db825535159a1ec976d" && ActorProfileReady();
}
bool Swap(uintptr_t address,void* expected,void* replacement) noexcept {
    auto slot=reinterpret_cast<PVOID volatile*>(address);
    DWORD old{},unused{};
    if(!VirtualProtect(reinterpret_cast<void*>(address),4,PAGE_READWRITE,&old)) return false;
    const bool exchanged=InterlockedCompareExchangePointer(slot,replacement,expected)==expected;
    if(VirtualProtect(reinterpret_cast<void*>(address),4,old,&unused)) return exchanged;
    // Keep the pinned callback code resident even if rollback is needed.
    if(exchanged) InterlockedCompareExchangePointer(slot,expected,replacement);
    VirtualProtect(reinterpret_cast<void*>(address),4,old,&unused);
    Log("[probe] protection restoration failed; adapter remains disabled");
    return false;
}
bool Install() noexcept {
    originalUpdate=reinterpret_cast<UpdateFn>(UpdateAddress);
    originalSystem=reinterpret_cast<SystemFn>(SystemAddress);
    if(!Swap(SystemSlot,reinterpret_cast<void*>(SystemAddress),reinterpret_cast<void*>(&SystemHook))) return false;
    if(!Swap(UpdateSlot,reinterpret_cast<void*>(UpdateAddress),reinterpret_cast<void*>(&UpdateHook))) {
        if(!Swap(SystemSlot,reinterpret_cast<void*>(&SystemHook),reinterpret_cast<void*>(SystemAddress)))
            Log("[probe] system-slot rollback incomplete; pinned forwarding callback retained");
        return false;
    }
    active.store(true,std::memory_order_release); return true;
}
DWORD WINAPI Run(void*) noexcept {
    try {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        wchar_t exe[32768]{},module[32768]{};
        const DWORD n=GetModuleFileNameW(self,module,32768),e=GetModuleFileNameW(nullptr,exe,32768);
        if(!n || n>=32768 || !e || e>=32768) return 0;
        wchar_t* slash=std::wcsrchr(module,L'\\');
        if(!slash || size_t(slash-module)+32>=32768) return 0;
        slash[1]=0; std::swprintf(logPath,32768,L"%lsCrysis3NativeProbe.log",module);
        Log("[probe] native read-only UI research trial; gameplay actions disabled");
        if(FileHash(exe,86636544)!=SourceHash) { Log("[probe] unsupported source; no callbacks installed"); return 0; }
        wchar_t ini[32768]{}; std::swprintf(ini,32768,L"%lsCrysis3NativeProbe.ini",module);
        if(!GetPrivateProfileIntW(L"Probe",L"Enabled",0,ini)) { Log("[probe] disabled by default; forwarding only"); return 0; }
#ifdef C3_NATIVE_UI
        if(GetPrivateProfileIntW(L"Probe",L"EnableUI",0,ini)) {
            std::swprintf(panelAssets,32768,L"%lsCrysis3PanelAssets",module);
            HANDLE panel=CreateThread(nullptr,0,PanelWorker,nullptr,0,nullptr);
            if(panel) CloseHandle(panel); else Log("[panel] worker creation failed; gameplay left untouched");
        }
#endif
        const uint64_t start=GetTickCount64(); bool ready=false;
        while(!stopping.load() && GetTickCount64()-start<120000) {
            if(ProfileReady()) { ready=true; break; }
            Sleep(500);
        }
        bool weaponReady=ready;
        if(weaponReady) for(const auto& range:c3::research::NativeWeaponProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { weaponReady=false; break; }
        weaponProfileVerified.store(weaponReady);
        bool namesReady=weaponReady;
        if(namesReady) for(const auto& range:c3::research::NativeWeaponNameProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { namesReady=false; break; }
        weaponNameProfileVerified.store(namesReady);
        bool attachmentsReady=namesReady;
        if(attachmentsReady) for(const auto& range:c3::research::NativeAttachmentProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { attachmentsReady=false; break; }
        attachmentProfileVerified.store(attachmentsReady);
        bool suitReady=ready;
        if(suitReady) for(const auto& range:c3native::NativeSuitProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { suitReady=false; break; }
        suitProfileVerified.store(suitReady);
        bool objectivesReady=ready;
        if(objectivesReady) for(const auto& range:c3native::NativeObjectiveProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { objectivesReady=false; break; }
        objectiveProfileVerified.store(objectivesReady);
        bool statsReady=ready;
        if(statsReady) for(const auto& range:c3native::NativeStatsProfile)
            if(MemoryHash(range.address,range.size)!=range.sha256) { statsReady=false; break; }
        statsProfileVerified.store(statsReady);
        if(!ready || !Install()) { Log("[probe] live profile/install rejected; observation disabled"); return 0; }
        Log(weaponReady?"[probe] exact weapon read profile accepted":"[probe] weapon read profile unavailable; vitals only");
        Log(namesReady?"[probe] exact owned weapon identity profile accepted":"[probe] owned weapon identity profile unavailable");
        Log(suitReady?"[probe] exact suit module read profile accepted":"[probe] suit module read profile unavailable");
        Log(objectivesReady?"[probe] exact objective read profile accepted":"[probe] objective read profile unavailable");
        Log(statsReady?"[probe] exact statistics read profile accepted":"[probe] statistics profile unavailable");
        Log("[probe] exact-profile callbacks installed; double-read actor/phase diagnostic, NOT verified telemetry");
        uint32_t seen{}; uint64_t lastLog{};
        while(!stopping.load()) {
            Sleep(200);
            const uint64_t now=GetTickCount64();
            std::array<SystemRecord,128> events{}; uint32_t dropped{};
            const size_t eventCount=systemJournal.Drain(events,dropped);
            if(eventCount || dropped) {
                // One bounded append per batch, never IO inside native callbacks.
                char batch[24576]{}; size_t used{};
                for(size_t i=0;i<eventCount;++i) {
                    const auto& e=events[i];
                    const int length=std::snprintf(batch+used,sizeof(batch)-used,
                        "system completionAt=%llu entryObservationEpoch=%lu thread=%lu rawEvent=%lu arg0=%08lx arg1=%08lx\n",
                        static_cast<unsigned long long>(e.at),static_cast<unsigned long>(e.epoch),
                        static_cast<unsigned long>(e.thread),static_cast<unsigned long>(e.event),
                        static_cast<unsigned long>(e.arg0),static_cast<unsigned long>(e.arg1));
                    if(length<0 || size_t(length)>=sizeof(batch)-used) break;
                    used+=size_t(length);
                }
                if(dropped) std::snprintf(batch+used,sizeof(batch)-used,"system journal dropped=%lu\n",static_cast<unsigned long>(dropped));
                Log(batch);
            }
            if(now-lastLog<1000 || revision.load(std::memory_order_acquire)==seen) continue;
            Observation frame{};
            AcquireSRWLockShared(&frameLock); frame=pending; seen=revision.load(); ReleaseSRWLockShared(&frameLock);
            if(now<frame.at || now-frame.at>750 || frame.epoch!=callbacks.ObservationEpoch() || callbacks.SystemDepth() ||
                callbacks.ThreadConflict() || callbacks.Exhausted()) { frame.consistent=false; frame.phase={}; }
            char line[640]{};
            std::snprintf(line,sizeof(line),"sample at=%llu observationEpoch=%lu thread=%lu consistent=%d entity=%08lx health=%.4f/%.4f energy=%.4f candidateMax=%.4f mode=%lu channel=%lu systemEvent=%lu systemThread=%lu wrongThreads=%lu adapterVerified=0",
                static_cast<unsigned long long>(frame.at),static_cast<unsigned long>(frame.epoch),static_cast<unsigned long>(frame.thread),int(frame.consistent),
                static_cast<unsigned long>(frame.entity),frame.health,frame.maxHealth,frame.energy,frame.candidateMaxEnergy,
                static_cast<unsigned long>(frame.mode),static_cast<unsigned long>(frame.channel),static_cast<unsigned long>(frame.systemEvent),
                static_cast<unsigned long>(frame.systemThread),static_cast<unsigned long>(frame.wrongThreads));
            Log(line); lastLog=now;
            char phaseLine[320]{};
            std::snprintf(phaseLine,sizeof(phaseLine),"phase actorRead=%lu phaseRead=%lu context=%lu present=%d active=%d pauseKnown=%d paused=%d cutsceneKnown=%d cutscene=%d adapterVerified=0",
                static_cast<unsigned long>(frame.actorReadFailure),static_cast<unsigned long>(frame.phaseReadFailure),
                static_cast<unsigned long>(frame.phase.contextState),int(frame.phase.contextPresent),int(frame.phase.environmentActive),
                int(frame.phase.pauseKnown),int(frame.phase.paused),int(frame.phase.cutsceneKnown),int(frame.phase.cutscene));
            Log(phaseLine);
            char trialLine[160]{};
            std::snprintf(trialLine,sizeof(trialLine),"ui-publication flags=%lu phase=%lu captureMs=%lu",
                static_cast<unsigned long>(trialPublishFlags.load()),static_cast<unsigned long>(trialPublishPhase.load()),
                static_cast<unsigned long>(trialCaptureMs.load()));
            Log(trialLine);
        }
    } catch(...) { active.store(false); Log("[probe] internal failure; pinned callbacks forward only"); }
    return 0;
}
BOOL CALLBACK LoadOriginal(PINIT_ONCE,PVOID,PVOID*) noexcept {
    wchar_t path[MAX_PATH]{}; UINT n=GetSystemWow64DirectoryW(path,MAX_PATH);
    if(!n || n>=MAX_PATH-13) n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n || n>=MAX_PATH-13) return FALSE;
    lstrcatW(path,L"\\dinput8.dll"); systemInput=LoadLibraryW(path);
    return systemInput && systemInput!=self;
}
template<class Function> Function InputExport(const char* name) noexcept {
    if(!InitOnceExecuteOnce(&originalOnce,LoadOriginal,nullptr,nullptr)) return nullptr;
    FARPROC address=GetProcAddress(systemInput,name); Function result{};
    static_assert(sizeof(result)==sizeof(address));
    std::memcpy(&result,&address,sizeof(result)); return result;
}
} // namespace c3probe
extern "C" DWORD WINAPI Crysis3ResearchProbeIdentity() { return 0x43335031; }
extern "C" HRESULT WINAPI Crysis3DirectInput8Create(HINSTANCE instance,DWORD version,REFIID iid,LPVOID* output,LPUNKNOWN outer) {
    using namespace c3probe;
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
    const auto create=InputExport<Create>("DirectInput8Create");
    if(!create) { if(output) *output=nullptr; return E_FAIL; }
    const HRESULT result=create(instance,version,iid,output,outer);
    if(SUCCEEDED(result) && !started.exchange(true)) {
        HMODULE pinned{};
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&self),&pinned)) {
            HANDLE worker=CreateThread(nullptr,0,Run,nullptr,0,nullptr);
            if(worker) CloseHandle(worker); else started.store(false);
        } else started.store(false);
    }
    return result;
}
// Preserve Wine's complete dinput8 export surface. These forward COM contracts,
// not game calls; registration exports are never invoked by our probe.
extern "C" HRESULT WINAPI Crysis3DllCanUnloadNow() {
    if(c3probe::started.load()) return S_FALSE; // Callback module is pinned.
    auto call=c3probe::InputExport<HRESULT(WINAPI*)()>("DllCanUnloadNow");
    return call?call():S_FALSE;
}
extern "C" HRESULT WINAPI Crysis3DllGetClassObject(REFCLSID clsid,REFIID iid,LPVOID* output) {
    auto call=c3probe::InputExport<HRESULT(WINAPI*)(REFCLSID,REFIID,LPVOID*)>("DllGetClassObject");
    if(!call) { if(output) *output=nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
    return call(clsid,iid,output);
}
extern "C" HRESULT WINAPI Crysis3DllRegisterServer() {
    auto call=c3probe::InputExport<HRESULT(WINAPI*)()>("DllRegisterServer");
    return call?call():E_NOTIMPL;
}
extern "C" HRESULT WINAPI Crysis3DllUnregisterServer() {
    auto call=c3probe::InputExport<HRESULT(WINAPI*)()>("DllUnregisterServer");
    return call?call():E_NOTIMPL;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) c3probe::self=module;
    if(reason==DLL_PROCESS_DETACH) c3probe::stopping.store(true);
    return TRUE;
}
