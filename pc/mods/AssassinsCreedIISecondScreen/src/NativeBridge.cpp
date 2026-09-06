#include <windows.h>
#include <atomic>
#include <array>
#include "NativeBridge.h"
#include "NativeRead.h"

namespace ac2ds {
namespace {
class LocalMemory final : public native::Memory {
public:
    bool Copy(uint32_t address, void* output, size_t bytes) const override {
        SIZE_T got{};
        return bytes <= 65536 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, bytes, &got) && got == bytes;
    }
};
using Peek = BOOL (WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
Peek original{};
StateStore* destination{};
NativeLogger logNative{};
std::atomic<DWORD> ownerThread{};
thread_local bool sampling{};
uint64_t lastSample{}, generation{1}, revision{1};
native::Roots previous{};
bool hadRoots{};

void Sample() {
    const auto thread = GetCurrentThreadId();
    DWORD expected{};
    if (ownerThread.compare_exchange_strong(expected, thread))
        logNative("native message pump resolved thread=%lu; read-only state adapter", thread);
    if (ownerThread.load() != thread || sampling) return;
    auto now = GetTickCount64();
    if (now - lastSample < 250) return;
    lastSample = now;
    sampling = true;
    struct Reset { ~Reset() { sampling = false; } } reset;
    LocalMemory memory; native::Roots roots{};
    bool valid = native::ReadRoots(memory, roots);
    if (!valid) roots = {};
    if (!hadRoots || !(roots == previous)) {
        ++generation; previous = roots; hadRoots = true;
        logNative("native generation=%llu valid=%d world=%08lx player=%08lx progress=%08lx",
            static_cast<unsigned long long>(generation), int(valid), static_cast<unsigned long>(roots.world),
            static_cast<unsigned long>(roots.player), static_cast<unsigned long>(roots.progress));
    }
    Snapshot snapshot; snapshot.generation = generation; snapshot.revision = ++revision;
    snapshot.sampledAt = now; snapshot.worldIdentity = roots.world; snapshot.playerIdentity = roots.player;
    snapshot.phase = Phase::Loading;
    snapshot.status = L"Waiting for a loaded memory";
    if (valid && roots.world && roots.player && roots.progress) {
        // These are native aggregate counters, never marker locations or locked entries.
        constexpr std::array<std::pair<uint32_t, const wchar_t*>, 4> collections{{
            {0x170, L"Feathers"}, {0x180, L"Codex pages"}, {0x184, L"Statuettes"}, {0x154, L"Assassin seals"}}};
        for (const auto& collection : collections) {
            auto value = native::ReadCollection(memory, roots.progress, collection.first);
            if (value) snapshot.statistics.push_back({collection.second, double(value->completed), double(value->total), false});
        }
        native::Roots after{};
        if (native::ReadRoots(memory, after) && roots == after) {
            snapshot.phase = Phase::Playing;
            snapshot.status = L"Memory connected";
            if (!snapshot.statistics.empty()) snapshot.capabilities |= Progress;
        } else {
            snapshot.statistics.clear(); snapshot.status = L"Synchronizing memory transition";
        }
    }
    destination->Publish(std::move(snapshot));
}
BOOL WINAPI Pump(LPMSG message, HWND window, UINT first, UINT last, UINT flags) noexcept {
    auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    BOOL result = original(message, window, first, last, flags);
    // Only the two verified calls in Scimitar's 00b06a40 message pump qualify.
    // The panel and Wine's own message loops never enter the adapter.
    if (!result && !window && !first && !last && flags == PM_REMOVE &&
        (caller == 0x00b06a5b || caller == 0x00b06a91)) {
        try { Sample(); }
        catch (...) { if (logNative) logNative("native sample discarded after internal exception"); }
    }
    return result;
}
}
bool InstallNativeBridge(StateStore& store, NativeLogger logger) {
    if (destination || !logger || reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != 0x400000) return false;
    LocalMemory memory;
    const unsigned char expected[]{0x55,0x8b,0xec,0x83,0xec,0x1c,0x56,0x8b,0x35,0x2c,0x33,0xca,0x01};
    unsigned char actual[sizeof(expected)]{};
    if (!memory.Copy(0x00b06a40, actual, sizeof(actual)) || std::memcmp(actual, expected, sizeof(actual))) return false;
    auto slot = reinterpret_cast<void**>(0x01ca332c);
    void* prior{}; if (!memory.Copy(0x01ca332c, &prior, sizeof(prior))) return false;
    FARPROC exported = GetProcAddress(GetModuleHandleW(L"user32.dll"), "PeekMessageA");
    void* address{}; std::memcpy(&address, &exported, sizeof(address));
    if (!address || address != prior) { logger("native hook refused: PeekMessageA already replaced"); return false; }
    DWORD protection{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    std::memcpy(&original, &prior, sizeof(original)); destination = &store; logNative = logger;
    void* replacement{}; Peek hook = Pump; std::memcpy(&replacement, &hook, sizeof(replacement));
    bool installed = InterlockedCompareExchangePointer(slot, replacement, prior) == prior;
    DWORD ignored{}; VirtualProtect(slot, sizeof(void*), protection, &ignored);
    if (!installed) { destination = nullptr; logger("native hook refused: import changed during installation"); }
    else logger("native hook installed IAT=01ca332c pump=00b06a40 interval=250ms; actions disabled");
    return installed;
}
}
