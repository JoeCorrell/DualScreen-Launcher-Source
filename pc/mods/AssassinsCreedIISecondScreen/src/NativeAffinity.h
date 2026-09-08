#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace ac2ds::affinity {
using LogFunction = void(*)(const char*,...);
using SetAffinity = DWORD_PTR(WINAPI*)(HANDLE,DWORD_PTR);
inline SetAffinity original{};
inline DWORD_PTR allowed{};
inline LogFunction log{};
inline uint64_t repairAt{};
inline std::atomic<unsigned> adjusted{0};
inline bool GameCaller(uintptr_t caller) {
    return caller==0x421eb4 || caller==0xc4f6db || caller==0xec573d;
}
__attribute__((noinline)) inline DWORD_PTR WINAPI Hook(HANDLE thread,DWORD_PTR mask) {
    auto caller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if (GameCaller(caller) && mask && !(mask & ~allowed)) {
        if (mask != allowed) adjusted.fetch_add(1,std::memory_order_relaxed);
        mask=allowed;
    }
    return original(thread,mask);
}
inline void RepairThreads() {
    HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if (list==INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry{}; entry.dwSize=sizeof(entry); unsigned changed{},failed{}; DWORD firstError{};
    if (Thread32First(list,&entry)) do {
        if (entry.th32OwnerProcessID!=GetCurrentProcessId()) continue;
        // SetThreadAffinityMask queries the previous mask as well as setting it.
        HANDLE thread=OpenThread(THREAD_SET_INFORMATION|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if (!thread) { if(!firstError)firstError=GetLastError(); ++failed; continue; }
        DWORD_PTR previous=original(thread,allowed);
        if (!previous) { if(!firstError)firstError=GetLastError(); ++failed; } else if (previous!=allowed) ++changed;
        CloseHandle(thread);
    } while (Thread32Next(list,&entry));
    CloseHandle(list);
    log("performance affinity mask=%lx repaired=%u unavailable=%u error=%lu legacyCalls=%u",
        static_cast<unsigned long>(allowed),changed,failed,static_cast<unsigned long>(firstError),adjusted.load(std::memory_order_relaxed));
}
// Called only after Runtime's SHA-256 build gate. Limit the patch to the exact
// measured Wine container topology; native Windows and other presets are untouched.
inline void Install(LogFunction logger) {
    // Read once before any affinity mutation. This permits a same-renderer,
    // same-translator comparison against the game's original scheduling.
    wchar_t executable[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if (length && length<32768) {
        std::wstring path(executable,length);
        const auto slash=path.find_last_of(L"\\/");
        if (slash!=std::wstring::npos) {
            path.resize(slash+1);
            path+=L"AssassinsCreedIISecondScreen.ini";
            if (GetPrivateProfileIntW(L"Diagnostics",L"NativeAffinity",1,path.c_str())==0) {
                logger("performance affinity disabled by Diagnostics.NativeAffinity=0; original game scheduling retained");
                return;
            }
        }
    }
    char topology[64]{};
    HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    if (!ntdll || !GetProcAddress(ntdll,"wine_get_version") ||
        GetEnvironmentVariableA("WINE_CPU_TOPOLOGY",topology,sizeof(topology))!=11 ||
        std::strcmp(topology,"5:3,4,5,6,7")) return;
    SYSTEM_INFO info{}; GetSystemInfo(&info); DWORD_PTR process{},system{};
    if (info.dwNumberOfProcessors!=5 || !GetProcessAffinityMask(GetCurrentProcess(),&process,&system) ||
        system!=0x1f || reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000) return;
    constexpr unsigned char signature[]{0xff,0x15,0x78,0x32,0xca,0x01};
    for (uintptr_t site : {uintptr_t(0x421eae),uintptr_t(0xc4f6d5),uintptr_t(0xec5737)}) {
        unsigned char bytes[sizeof(signature)]{}; SIZE_T copied{};
        if (!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(site),bytes,sizeof(bytes),&copied) ||
            copied!=sizeof(bytes) || std::memcmp(bytes,signature,sizeof(bytes))) return;
    }
    auto api=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"SetThreadAffinityMask");
    auto* slot=reinterpret_cast<void* volatile*>(0x1ca3278);
    void* expected{}; SIZE_T copied{};
    if (!api || !ReadProcessMemory(GetCurrentProcess(),const_cast<void**>(slot),&expected,sizeof(expected),&copied) ||
        copied!=sizeof(expected) || expected!=reinterpret_cast<void*>(api)) return;
    DWORD protection{};
    if (!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&protection)) return;
    std::memcpy(&original,&api,sizeof(original)); allowed=system; log=logger;
    if (!SetProcessAffinityMask(GetCurrentProcess(),allowed)) {
        DWORD unused{}; VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&unused); return;
    }
    void* previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&Hook),expected);
    DWORD unused{}; VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&unused);
    if (previous!=expected) { SetProcessAffinityMask(GetCurrentProcess(),process); return; }
    log("performance affinity installed process=%lx -> %lx topology=%s; verified game call sites only",
        static_cast<unsigned long>(process),static_cast<unsigned long>(allowed),topology);
    RepairThreads();
    // One delayed repair includes graphics/runtime workers created during startup.
    // No periodic thread enumeration or memory scanning in the frame path.
    repairAt=GetTickCount64()+10000;
}
inline void Tick(uint64_t now) {
    if (repairAt && now>=repairAt) { repairAt=0; RepairThreads(); }
}
}
