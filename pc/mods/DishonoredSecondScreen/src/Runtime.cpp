#include "Runtime.h"
#include "StateAdapter.h"
#include "TargetBuild.h"
#include "FaultCapture.h"
#include "UpperViewport.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <memory>
#ifndef DISHONORED_BUILD_ID
#define DISHONORED_BUILD_ID "untracked-local-build"
#endif

namespace dishonored {
namespace {
// This translation unit is linked only into the full payload. Keep the
// artifact check independent of hook-log wording and the diagnostic proxy.
constexpr char FullPayloadIdentity[] = "DishonoredSecondScreen.FullPayload.i386.v1";
using TickFunction = int (__attribute__((fastcall)) *)(void*, void*, float, int);
using TeardownFunction = void (__attribute__((fastcall)) *)(void*, void*);
using RestoreFunction = void (__attribute__((fastcall)) *)(void*, void*, void*, int);
using MarkerRenderFunction = void (__attribute__((fastcall)) *)(void*, void*, float, float, uint32_t, uint32_t, float, uint32_t);
using NoteInteractFunction = uint32_t (__attribute__((fastcall)) *)(void*, void*, void*);
std::atomic<uintptr_t> originalTick{}, originalTeardown{}, originalRestore{}, originalMarkerRender{}, originalNoteInteract{};
std::atomic<uint64_t> worldEpoch{};
std::atomic<uint32_t> restoreDepth{};
std::atomic<DWORD> tickThread{};
SRWLOCK adapterLock = SRWLOCK_INIT;
StateAdapter* activeAdapter{}; // Protected by adapterLock, including destruction.
Bridge* activeBridge{}; // Same publication/lifetime lock as activeAdapter.
UpperViewport upperViewport;
uint64_t observedEpoch{}; // Solely the established game thread owns this value.
Address runtimeImage{};
thread_local bool adapterBusy{};

template<class T> bool Read(Address address, T& value) noexcept {
    return Memory::Span(address, sizeof(T)) && ReadProcessBytes(address, &value, sizeof(T), nullptr);
}

int __attribute__((fastcall)) TickHook(void* receiver, void*, float delta, int kind) noexcept {
    const auto original = reinterpret_cast<TickFunction>(originalTick.load(std::memory_order_acquire));
    const int result = original(receiver, nullptr, delta, kind);
    if (restoreDepth.load(std::memory_order_acquire)) return result;
    Address controller{};
    if (!Read(runtimeImage + ControllerGlobal, controller) || controller != uintptr_t(receiver)) return result;
    DWORD expected{}, thread = GetCurrentThreadId();
    tickThread.compare_exchange_strong(expected, thread, std::memory_order_acq_rel);
    if (tickThread.load(std::memory_order_acquire) != thread) return result;
    if (adapterBusy || !TryAcquireSRWLockShared(&adapterLock)) return result;
    if (activeAdapter) {
        adapterBusy = true;
        const uint64_t now = GetTickCount64(), epoch = worldEpoch.load(std::memory_order_acquire);
        if (activeBridge && upperViewport.Tick(runtimeImage + ViewportWindowProcedure, now, *activeBridge)) {
            adapterBusy = false;
            ReleaseSRWLockShared(&adapterLock);
            return result;
        }
        if (epoch != observedEpoch) {
            observedEpoch = epoch;
            activeAdapter->Invalidate(bridge::Lifecycle::NoPlayer, now);
        }
        activeAdapter->Tick(controller, now);
        adapterBusy = false;
    }
    ReleaseSRWLockShared(&adapterLock);
    return result;
}
void __attribute__((fastcall)) MarkerRenderHook(void* receiver, void*, float x, float y,
    uint32_t third, uint32_t fourth, float distance, uint32_t flags) noexcept {
    // 00bc5830 forwards all six stack arguments to 00bbd4f0, then formats
    // distance*0.01. Capture that input only after the native render returns.
    // Never reinterpret screen coordinates as map coordinates or read +80's
    // stale distance cache. The two opaque arguments are forwarded bitwise.
    reinterpret_cast<MarkerRenderFunction>(originalMarkerRender.load(std::memory_order_acquire))(
        receiver, nullptr, x, y, third, fourth, distance, flags);
    if (adapterBusy || restoreDepth.load(std::memory_order_acquire) ||
        tickThread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
        worldEpoch.load(std::memory_order_acquire) != observedEpoch ||
        !TryAcquireSRWLockShared(&adapterLock)) return;
    if (activeAdapter) {
        const Address marker = Address(uintptr_t(receiver));
        std::array<uint32_t, 14> header{}; int32_t index{};
        // The native marker is 88h bytes. One prefix read plus its actor's
        // index keeps callback overhead bounded to two OS memory reads.
        if (Read(marker, header) && header[0] == runtimeImage + TaskMarkerVtable &&
            Read(header[2] + layout::ObjectIndex, index) && index >= 0) {
            if ((header[7] & 0x8f) == 1 || !header[8]) flags = 0;
            activeAdapter->ObserveTaskMarker(marker, header[2], index, distance, flags, GetTickCount64());
        }
    }
    ReleaseSRWLockShared(&adapterLock);
}
void __attribute__((fastcall)) TeardownHook(void* receiver, void*) noexcept {
    // 00ab5100 clears the global pawn during its native teardown. Increment
    // before forwarding, covering destruction followed by address/index reuse.
    worldEpoch.fetch_add(1, std::memory_order_acq_rel);
    reinterpret_cast<TeardownFunction>(originalTeardown.load(std::memory_order_acquire))(receiver, nullptr);
}
uint32_t __attribute__((fastcall)) NoteInteractHook(void* receiver, void*, void* pawn) noexcept {
    // Native A36050 sets the acquired flag before returning. Its EAX result
    // controls original interaction handling, so forward it unchanged even
    // when we cannot observe the actor or the note is not a map board.
    const auto result = reinterpret_cast<NoteInteractFunction>(originalNoteInteract.load(std::memory_order_acquire))(
        receiver, nullptr, pawn);
    if (adapterBusy || restoreDepth.load(std::memory_order_acquire) ||
        tickThread.load(std::memory_order_acquire) != GetCurrentThreadId() ||
        worldEpoch.load(std::memory_order_acquire) != observedEpoch ||
        !TryAcquireSRWLockShared(&adapterLock)) return result;
    if (activeAdapter) {
        const Address actor = Address(uintptr_t(receiver)); std::array<uint32_t, 14> header{};
        if (Read(actor, header) && header[0] == runtimeImage + NoteVtable && header[1] <= INT32_MAX && header[9])
            activeAdapter->ObserveMapBoard(actor, header[9], int32_t(header[1]));
    }
    ReleaseSRWLockShared(&adapterLock);
    return result;
}
void __attribute__((fastcall)) RestoreHook(void* receiver, void*, void* archive, int flags) noexcept {
    // Corvo virtual +118 -> 00ab8790: ECX is the pawn, two stack arguments,
    // native epilogue 00ab8bde ret8. Loading can reuse the same pawn/index.
    // Suppress nested samples while native restoration replaces its contents.
    restoreDepth.fetch_add(1, std::memory_order_acq_rel);
    worldEpoch.fetch_add(1, std::memory_order_acq_rel);
    reinterpret_cast<RestoreFunction>(originalRestore.load(std::memory_order_acquire))(receiver, nullptr, archive, flags);
    worldEpoch.fetch_add(1, std::memory_order_acq_rel);
    restoreDepth.fetch_sub(1, std::memory_order_acq_rel);
}

struct Hook {
    Address slot{};
    uintptr_t original{}, replacement{};
    bool installed{};
    bool Install(Address at, uintptr_t expected, uintptr_t next, std::atomic<uintptr_t>& forwarding) noexcept {
        uintptr_t current{};
        if (!Read(at, current) || current != expected || (at & 3)) return false;
        forwarding.store(expected, std::memory_order_release);
        auto* pointer = reinterpret_cast<PVOID volatile*>(uintptr_t(at));
        DWORD protection{};
        if (!VirtualProtect(const_cast<PVOID*>(pointer), sizeof(void*), PAGE_READWRITE, &protection)) return false;
        const auto previous = InterlockedCompareExchangePointer(pointer, reinterpret_cast<void*>(next), reinterpret_cast<void*>(expected));
        DWORD unused{};
        const bool restored = VirtualProtect(const_cast<PVOID*>(pointer), sizeof(void*), protection, &unused) != FALSE;
        if (previous == reinterpret_cast<void*>(expected)) {
            slot = at; original = expected; replacement = next; installed = true;
        }
        return installed && restored;
    }
    bool Remove() noexcept {
        if (!installed) return true;
        auto* pointer = reinterpret_cast<PVOID volatile*>(uintptr_t(slot));
        DWORD protection{};
        if (!VirtualProtect(const_cast<PVOID*>(pointer), sizeof(void*), PAGE_READWRITE, &protection)) return false;
        // Do not overwrite another mod's later replacement. This module is
        // pinned by the bootstrap, so a chained reference remains callable.
        const auto previous = InterlockedCompareExchangePointer(pointer,
            reinterpret_cast<void*>(original), reinterpret_cast<void*>(replacement));
        DWORD unused{};
        const bool restored = VirtualProtect(const_cast<PVOID*>(pointer), sizeof(void*), protection, &unused) != FALSE;
        installed = false;
        return restored && (previous == reinterpret_cast<void*>(replacement) || previous == reinterpret_cast<void*>(original));
    }
};
struct Runtime {
    Bridge bridge;
    Definitions definitions;
    FaultCapture faults;
    std::unique_ptr<StateAdapter> adapter;
    Hook tick, teardown, restore, markerRender, noteInteract;
    LogFunction log{};
    void Detach() noexcept {
        // Worker may wait for the current sampler to finish; game hooks only
        // try-lock and never wait for worker/UI/rendering work.
        AcquireSRWLockExclusive(&adapterLock);
        activeAdapter = nullptr;
        activeBridge = nullptr;
        ReleaseSRWLockExclusive(&adapterLock);
        const bool tickRemoved = tick.Remove(), teardownRemoved = teardown.Remove(), restoreRemoved = restore.Remove();
        const bool markerRemoved = markerRender.Remove();
        const bool noteRemoved = noteInteract.Remove();
        if (log) log(tickRemoved && teardownRemoved && restoreRemoved && markerRemoved && noteRemoved ? "[hooks] detached; adapter quiescent" :
            "[hooks] restoration conflict; forwarding callbacks remain pinned, adapter withdrawn");
    }
    ~Runtime() { Detach(); }
};
bool Language(Address image, std::wstring& language) {
    // appLocalize 00488d90 uses this inline wide character array when the
    // caller supplies no language. It is the active language, not an INI guess.
    std::array<wchar_t, 4> text{};
    if (!Read(image + 0xe5e660, text) || text[3]) return false;
    for (uint32_t i = 0; i < 3; ++i) {
        if (text[i] >= L'a' && text[i] <= L'z') text[i] -= L'a' - L'A';
        if (text[i] < L'A' || text[i] > L'Z') return false;
    }
    language.assign(text.data(), 3); return true;
}
bool Initialize(Runtime* runtime, Address image, const std::wstring& directory,
    std::atomic<bool>& stopping, LogFunction log) {
    std::wstring language;
    uint64_t nextLanguageLog{};
    while (!stopping.load(std::memory_order_acquire) && !Language(image, language)) {
        // The proxy can initialize before appInit has filled the language
        // buffer. Wait on this worker; never stall the game or invent INT.
        const uint64_t now = GetTickCount64();
        if (log && now >= nextLanguageLog) {
            log("[localization] waiting for the game's active language"); nextLanguageLog = now + 10000;
        }
        Sleep(100);
    }
    if (stopping.load(std::memory_order_acquire)) return false;
    const auto rpg = directory + L"..\\..\\DishonoredGame\\Localization\\" + language + L"\\RPG." + language;
    if (!runtime->definitions.LoadRpg(rpg)) {
        if (log) log("[localization] original RPG definitions unavailable");
        return false;
    }
    // Loading-screen 00bc86e0 selects DishonoredGame.<active language>,
    // falling back to INT only when its MapNames section is absent.
    const auto localization = directory + L"..\\..\\DishonoredGame\\Localization\\";
    bool mapNames = runtime->definitions.LoadMapNames(localization + language + L"\\DishonoredGame." + language);
    if (!mapNames && language != L"INT")
        mapNames = runtime->definitions.LoadMapNames(localization + L"INT\\DishonoredGame.INT");
    if (log) log(mapNames ? "[localization] original area names ready" : "[localization] area names unavailable");
    runtime->adapter = std::make_unique<StateAdapter>(image, runtime->bridge, &runtime->definitions, true);
    std::array<uint8_t, 9> tickBytes{};
    constexpr std::array<uint8_t, 9> expected = {0x53, 0x8b, 0xdc, 0x83, 0xec, 0x08, 0x83, 0xe4, 0xf0};
    if (!Read(image + ControllerTick, tickBytes) || tickBytes != expected) {
        if (log) log("[hooks] controller tick bytes differ from verified executable");
        return false;
    }
    // Set all immutable context before publishing callbacks. Adapter
    // publication occurs only after every hook installation has succeeded.
    if (!runtime->teardown.Install(image + PawnVtable + PawnTeardownSlot, image + PawnTeardown,
            reinterpret_cast<uintptr_t>(&TeardownHook), originalTeardown) ||
        !runtime->restore.Install(image + PawnVtable + PawnRestoreSlot, image + PawnRestore,
            reinterpret_cast<uintptr_t>(&RestoreHook), originalRestore) ||
        !runtime->markerRender.Install(image + TaskMarkerVtable + TaskMarkerRenderSlot, image + TaskMarkerRender,
            reinterpret_cast<uintptr_t>(&MarkerRenderHook), originalMarkerRender) ||
        !runtime->noteInteract.Install(image + NoteVtable + NoteInteractSlot, image + NoteInteract,
            reinterpret_cast<uintptr_t>(&NoteInteractHook), originalNoteInteract) ||
        !runtime->tick.Install(image + ControllerVtable + TickSlot, image + ControllerTick,
            reinterpret_cast<uintptr_t>(&TickHook), originalTick)) {
        if (log) log("[hooks] exact vtable validation/installation failed");
        return false;
    }
    AcquireSRWLockExclusive(&adapterLock);
    activeAdapter = runtime->adapter.get();
    activeBridge = &runtime->bridge;
    ReleaseSRWLockExclusive(&adapterLock);
    if (log) log("[hooks] controller tick, Corvo restore/teardown, task marker render and note interaction installed; game-thread adapter enabled");
    return true;
}

struct Initializer {
    Runtime* runtime{};
    Address image{};
    const std::wstring* directory{};
    std::atomic<bool>* stopping{};
    HANDLE thread{};
    static DWORD WINAPI Run(void* parameter) noexcept {
        auto& context = *static_cast<Initializer*>(parameter);
        bool ready{};
        try { ready = Initialize(context.runtime, context.image, *context.directory, *context.stopping, context.runtime->log); }
        catch (...) { if (context.runtime->log) context.runtime->log("[bootstrap] game-state initialization failed with an internal exception"); }
        if (!ready) context.runtime->Detach();
        context.runtime->bridge.bootstrap.store(ready ? Bridge::Bootstrap::Ready : Bridge::Bootstrap::Failed, std::memory_order_release);
        return 0;
    }
    Initializer(Runtime& owner, Address base, const std::wstring& path, std::atomic<bool>& stop)
        : runtime(&owner), image(base), directory(&path), stopping(&stop) {}
    Initializer(const Initializer&) = delete;
    Initializer& operator=(const Initializer&) = delete;
    ~Initializer() {
        // Covers panel failures and exception unwinding as well as normal
        // shutdown. Join before Runtime/Definitions/Bridge can be destroyed.
        stopping->store(true, std::memory_order_release);
        if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    }
};
} // namespace

bool RunRuntime(Address image, const std::wstring& directory, std::atomic<bool>& stopping, LogFunction log) {
    static_assert(sizeof(void*) == 4, "Dishonored payload must be compiled for i386");
    if (log) log(FullPayloadIdentity);
    if (log) log("[build] sourceSha256=" DISHONORED_BUILD_ID);
    auto runtime = std::make_unique<Runtime>(); runtime->log = log;
    const bool faults = runtime->faults.Open(directory + L"DishonoredSecondScreen.native-faults.bin");
    if (log) log(faults ? "[faults] bounded native exception observer ready" : "[faults] diagnostic file unavailable or at its size limit");
    runtimeImage = image;
    Initializer initializer(*runtime, image, directory, stopping);
    initializer.thread = CreateThread(nullptr, 0, Initializer::Run, &initializer, 0, nullptr);
    if (!initializer.thread) {
        runtime->bridge.bootstrap.store(Bridge::Bootstrap::Failed, std::memory_order_release);
        if (log) log("[bootstrap] game-state initializer thread could not start");
    }
    // Like Skyrim's proxy/plugin separation, the CPU/GDI panel initializes
    // independently of language readiness, player objects and game hooks.
    // Hook failure leaves an honest waiting/error UI; it cannot impersonate
    // a live snapshot or change the top-screen swap chain.
    return RunPanel(runtime->bridge, directory + L"DishonoredSecondScreen\\assets",
        directory + L"DishonoredSecondScreen.ini", stopping, log);
}
} // namespace dishonored
