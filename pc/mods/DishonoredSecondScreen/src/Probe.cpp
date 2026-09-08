// Development-only game-thread proof. This is not the finished UI payload.
#include <windows.h>
#include <bcrypt.h>
#include <dinput.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "TargetBuild.h"
#ifdef DISHONORED_FULL_PAYLOAD
#include "Runtime.h"
#endif

namespace {
HMODULE self{};
HMODULE originalLibrary{};
INIT_ONCE originalOnce = INIT_ONCE_STATIC_INIT;
INIT_ONCE logOnce = INIT_ONCE_STATIC_INIT;
std::atomic<bool> started{false}, stopping{false};
std::atomic<bool> firstExport{true};
uintptr_t base{};
std::wstring logPath;
SRWLOCK logLock = SRWLOCK_INIT;
// The extra fastcall EDX argument is unused by the original thiscall method;
// ECX is the receiver and the two stack arguments/RET 8 remain identical.
#ifndef DISHONORED_FULL_PAYLOAD
using Tick = int(__attribute__((fastcall)) *)(void*, void*, float, int);
Tick originalTick{};
struct Sample {
    uint32_t thread{}, controller{}, pawn{}, generation{};
    int health{}, healthMax{}, mana{}, manaMax{}, activePowers{}, collectedCharms{};
    bool live{};
};
SRWLOCK sampleLock = SRWLOCK_INIT;
Sample sample{};
std::atomic<uint32_t> sampleRevision{0};
#endif

// ReadProcessMemory fails on inaccessible pages; no unchecked game pointer
// dereferences, SEH suppression or process-wide exception handler is used.
bool ReadBytes(uintptr_t address, void* output, size_t size) noexcept {
    SIZE_T read{};
    if (address < 0x10000 || size > 65536 || address + size < address) return false;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &read) && read == size;
}
template<class T> bool Read(uintptr_t address, T& output) noexcept { return ReadBytes(address, &output, sizeof(output)); }
bool Verify() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return false;
    std::wstring executable(path);
    auto slash = executable.find_last_of(L"\\/");
    if (slash == std::wstring::npos || _wcsicmp(path + slash + 1, L"Dishonored.exe")) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart == dishonored::ExecutableBytes;
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    DWORD objectSize{}, returned{};
    ok = ok && BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &returned, 0) >= 0 && objectSize <= 65536;
    std::vector<UCHAR> object(ok ? objectSize : 0), buffer(65536);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    while (ok) {
        DWORD count{};
        if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, buffer.data(), count, 0) >= 0;
    }
    UCHAR digest[32]{};
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    char hex[65]{};
    for (size_t i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (!ok || std::strcmp(hex, dishonored::ExecutableSha256)) return false;
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 pe{};
    return Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 4096 &&
        Read(base + dos.e_lfanew, pe) && pe.Signature == IMAGE_NT_SIGNATURE && pe.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        pe.FileHeader.TimeDateStamp == dishonored::Timestamp && pe.OptionalHeader.SizeOfImage == dishonored::ImageSize;
}
void Log(const char* text) noexcept {
    if (logPath.empty()) return;
    // Panel and game-state initialization now run independently, as in Skyrim.
    // Serialize worker-side rotation/appends; game hooks use the bounded queue.
    AcquireSRWLockExclusive(&logLock);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &data) && (data.nFileSizeHigh || data.nFileSizeLow > 1024 * 1024))
        MoveFileExW(logPath.c_str(), (logPath + L".previous").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* file = _wfopen(logPath.c_str(), L"ab");
    if (file) {
        std::fprintf(file, "[%llu] %s\n", static_cast<unsigned long long>(GetTickCount64()), text);
        std::fclose(file);
    }
    ReleaseSRWLockExclusive(&logLock);
}
BOOL CALLBACK PrepareLog(PINIT_ONCE, PVOID, PVOID*) noexcept {
    // Called from the export, outside DllMain/loader-lock initialization. This
    // must precede native forwarding and executable verification so failures
    // at either boundary leave a diagnostic instead of an absent log.
    try {
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(self, path, 32768);
        if (!length || length >= 32768) return FALSE;
        const std::wstring module(path);
        const auto slash = module.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return FALSE;
#ifdef DISHONORED_FULL_PAYLOAD
        logPath = module.substr(0, slash + 1) + L"DishonoredSecondScreen.log";
#else
        logPath = module.substr(0, slash + 1) + L"DishonoredSecondScreen.probe.log";
#endif
        return TRUE;
    } catch (...) { return FALSE; }
}
#ifndef DISHONORED_FULL_PAYLOAD
struct ArrayHeader { uint32_t data; int32_t count, capacity; };
bool ArrayCount(uintptr_t address, int& count, int limit, size_t stride) noexcept {
    ArrayHeader value{};
    if (!Read(address, value) || value.count < 0 || value.count > limit || value.capacity < value.count || value.capacity > 65536) return false;
    if (value.count) {
        std::array<unsigned char, 2048> bytes{};
        if (size_t(value.count) * stride > bytes.size() || !ReadBytes(value.data, bytes.data(), size_t(value.count) * stride)) return false;
    }
    count = value.count; return true;
}
int __attribute__((fastcall)) TickHook(void* controller, void*, float delta, int tickType) noexcept {
    // Preserve the original return value, stack arguments and execution order.
    int result = originalTick(controller, nullptr, delta, tickType);
    if (stopping.load(std::memory_order_relaxed)) return result;
    static std::atomic<DWORD> ownerThread{0};
    static uint64_t lastSample{};
    static uint32_t previousPawn{}, generation{};
    DWORD thread = GetCurrentThreadId();
    // These locals are owned exclusively by the engine tick thread after first
    // entry. Installation only targets the concrete Corvo controller vtable.
    DWORD expectedThread = 0;
    ownerThread.compare_exchange_strong(expectedThread, thread);
    if (ownerThread.load() != thread) return result;
    auto now = GetTickCount64();
    if (now - lastSample < 100) return result;
    lastSample = now;
    Sample next{}; next.thread = thread; next.controller = reinterpret_cast<uintptr_t>(controller);
    uint32_t currentController{}, pawn{}, currentPawn{}, pawnClass{}, actualClass{}, vtable{};
    next.live = Read(base + dishonored::ControllerGlobal, currentController) && currentController == next.controller &&
        Read(next.controller + dishonored::ControllerPawn, pawn) && pawn &&
        Read(base + dishonored::PawnGlobal, currentPawn) && pawn == currentPawn &&
        Read(base + dishonored::PawnClassGlobal, pawnClass) && pawnClass &&
        Read(pawn + dishonored::ObjectClass, actualClass) && actualClass == pawnClass &&
        Read(pawn, vtable) && vtable == base + dishonored::PawnVtable &&
        Read(pawn + dishonored::PawnHealth, next.health) && Read(pawn + dishonored::PawnHealthMax, next.healthMax) &&
        Read(pawn + dishonored::PawnMana, next.mana) && Read(pawn + dishonored::PawnManaMax, next.manaMax) &&
        ArrayCount(pawn + dishonored::PawnActivePowers, next.activePowers, 64, 4) &&
        ArrayCount(pawn + dishonored::PawnCollectedCharms, next.collectedCharms, 256, 8);
    if (next.live) next.pawn = pawn;
    if (previousPawn != next.pawn) { ++generation; previousPawn = next.pawn; }
    next.generation = generation;
    // Neither logging nor waiting for a renderer occurs on the game thread.
    if (TryAcquireSRWLockExclusive(&sampleLock)) {
        sample = next; sampleRevision.fetch_add(1, std::memory_order_release); ReleaseSRWLockExclusive(&sampleLock);
    }
    return result;
}
bool Install() noexcept {
    uintptr_t target{};
    if (!Read(base + dishonored::ControllerVtable + dishonored::TickSlot, target) || target != base + dishonored::ControllerTick) return false;
    const unsigned char expected[] = {0x53, 0x8b, 0xdc, 0x83, 0xec, 0x08, 0x83, 0xe4, 0xf0};
    unsigned char actual[sizeof(expected)]{};
    if (!ReadBytes(target, actual, sizeof(actual)) || std::memcmp(actual, expected, sizeof(actual))) return false;
    std::memcpy(&originalTick, &target, sizeof(target));
    auto slot = reinterpret_cast<PVOID volatile*>(base + dishonored::ControllerVtable + dishonored::TickSlot);
    DWORD protection{};
    if (!VirtualProtect(const_cast<PVOID*>(slot), sizeof(void*), PAGE_READWRITE, &protection)) return false;
    auto previous = InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(&TickHook), reinterpret_cast<void*>(target));
    DWORD unused{};
    VirtualProtect(const_cast<PVOID*>(slot), sizeof(void*), protection, &unused);
    return previous == reinterpret_cast<void*>(target);
}
#endif
DWORD WINAPI Run(void*) noexcept {
    try {
        Log("[bootstrap] worker entered; verifying executable before any hooks");
        if (!Verify()) { Log("unsupported executable; no hook installed"); return 0; }
        const auto ini = logPath.substr(0, logPath.find_last_of(L"\\/") + 1) + L"DishonoredSecondScreen.ini";
#ifdef DISHONORED_FULL_PAYLOAD
        if (!GetPrivateProfileIntW(L"Mod", L"Enabled", 1, ini.c_str())) {
            Log("[bootstrap] exact build accepted; payload disabled by configuration"); return 0;
        }
        Log("[bootstrap] exact executable SHA-256 and PE accepted; initializing independent payload");
        dishonored::RunRuntime(static_cast<dishonored::Address>(base),
            logPath.substr(0, logPath.find_last_of(L"\\/") + 1), stopping, Log);
#else
        if (!GetPrivateProfileIntW(L"Probe", L"Enabled", 0, ini.c_str())) {
            Log("exact build accepted; diagnostic hook disabled for baseline gameplay"); return 0;
        }
        if (!Install()) { Log("tick validation/install failed; no state adapter available"); return 0; }
        Log("exact build accepted; Corvo tick probe installed; read-only; no equipment/power/charm actions");
        uint32_t revision{}; Sample previous{};
        while (!stopping.load(std::memory_order_relaxed)) {
            Sleep(100);
            auto current = sampleRevision.load(std::memory_order_acquire);
            if (current == revision) continue;
            Sample next{};
            AcquireSRWLockShared(&sampleLock); next = sample; ReleaseSRWLockShared(&sampleLock); revision = current;
            if (next.thread == previous.thread && next.pawn == previous.pawn && next.live == previous.live && next.health == previous.health &&
                next.healthMax == previous.healthMax && next.mana == previous.mana && next.manaMax == previous.manaMax &&
                next.activePowers == previous.activePowers && next.collectedCharms == previous.collectedCharms) continue;
            char line[512]{};
            std::snprintf(line, sizeof(line), "state thread=%lu generation=%lu live=%d controller=%08lx pawn=%08lx health=%d/%d mana=%d/%d activePowers=%d collectedCharms=%d",
                static_cast<unsigned long>(next.thread), static_cast<unsigned long>(next.generation), int(next.live), static_cast<unsigned long>(next.controller),
                static_cast<unsigned long>(next.pawn), next.health, next.healthMax, next.mana, next.manaMax, next.activePowers, next.collectedCharms);
            Log(line); previous = next;
        }
#endif
    } catch (...) { Log("[bootstrap] worker stopped after internal exception; native hooks withdrawn"); }
    return 0;
}
BOOL CALLBACK LoadOriginal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{};
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length >= MAX_PATH - 13) return FALSE;
    lstrcatW(path, L"\\dinput8.dll");
    originalLibrary = LoadLibraryW(path);
    return originalLibrary && originalLibrary != self;
}
}
extern "C" HRESULT WINAPI DishonoredDirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    InitOnceExecuteOnce(&logOnce, PrepareLog, nullptr, nullptr);
    const bool diagnostic = firstExport.exchange(false);
    if (diagnostic) {
        char line[192]{};
        std::snprintf(line, sizeof(line), "[proxy] DirectInput8Create entered pid=%lu module=%p version=%lu",
            static_cast<unsigned long>(GetCurrentProcessId()), static_cast<void*>(self), static_cast<unsigned long>(version));
        Log(line);
    }
    if (!InitOnceExecuteOnce(&originalOnce, LoadOriginal, nullptr, nullptr)) {
        if (diagnostic) { char line[128]{}; std::snprintf(line, sizeof(line), "[proxy] system DirectInput load failed error=%lu", static_cast<unsigned long>(GetLastError())); Log(line); }
        return E_FAIL;
    }
    using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    FARPROC address = GetProcAddress(originalLibrary, "DirectInput8Create"); Create create{};
    std::memcpy(&create, &address, sizeof(create));
    if (!create) { if (diagnostic) Log("[proxy] system DirectInput8Create export missing"); return E_FAIL; }
    HRESULT result = create(instance, version, iid, output, outer);
    if (diagnostic) {
        char line[192]{};
        std::snprintf(line, sizeof(line), "[proxy] system DirectInput module=%p result=0x%08lx",
            static_cast<void*>(originalLibrary), static_cast<unsigned long>(result));
        Log(line);
    }
    if (SUCCEEDED(result) && !started.exchange(true)) {
        HMODULE pinned{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&self), &pinned)) {
            HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
            else { started.store(false); Log("[proxy] worker creation failed; next successful DirectInput call may retry"); }
        }
        else { started.store(false); Log("[proxy] module pin failed; next successful DirectInput call may retry"); }
    }
    return result;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self = module;
    if (reason == DLL_PROCESS_DETACH) stopping.store(true, std::memory_order_relaxed);
    return TRUE;
}
