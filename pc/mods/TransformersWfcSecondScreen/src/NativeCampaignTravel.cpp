#include "NativeCampaignTravel.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Hook.h"
#include "Log.h"
#include "NativeScriptAbi.h"

namespace wfcds {
namespace {

// Verified against the supported TWFC.exe and cooked export 20991.
// FinalCountdown.Tick 10A9BC8E sets EAX=script-this, then CALL 10A9B300.
// StartLevel records Start/statistics, builds and modifies the map URL, and
// calls ServerTravelToMap. It is not dispatched through a UFunction executor.
constexpr uintptr_t kStartLevelRva = 0x0019B300u;
constexpr uintptr_t kStartLevelCallsiteRva = 0x0019BC8Eu;
constexpr uint8_t kStartLevelHead[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x1C};
constexpr uint8_t kStartLevelCallsite[] = {
    0x8B, 0xC6, 0xE8, 0x6B, 0xF6, 0xFF, 0xFF};
using StartLevelFn = void (__attribute__((regparm(1))) *)(void* script_this);

InlineHook g_travel_hook;
void* g_travel_original = nullptr;
CampaignTravelAdmission g_admit_travel = nullptr;
DWORD g_travel_thread = 0;
std::atomic<bool> g_travel_stopping{false};
thread_local bool g_dispatching_travel = false;

void __attribute__((regparm(1))) HookedNativeStartLevel(void* script_this) {
  auto original = reinterpret_cast<StartLevelFn>(g_travel_original);
  if (!original) return;  // Original is published before installing the jump.
  if (g_travel_stopping.load(std::memory_order_acquire) ||
      GetCurrentThreadId() != g_travel_thread || !g_admit_travel) {
    original(script_this);
    return;
  }
  // No overlapping/reentrant chapter starts while the menu is being admitted.
  if (g_dispatching_travel) return;
  struct DispatchScope {
    DispatchScope() { g_dispatching_travel = true; }
    ~DispatchScope() { g_dispatching_travel = false; }
  } scope;
  const auto value = reinterpret_cast<uintptr_t>(script_this);
  // Compiled script uses 0x10 as its null-object sentinel. Never wrap it into
  // an apparently non-null UObject or feed it to custom state validation.
  if (value <= kNativeScriptThisOffset) {
    original(script_this);
    return;
  }
  void* lobby = reinterpret_cast<void*>(value - kNativeScriptThisOffset);
  if (g_admit_travel(lobby)) original(script_this);
}

bool InstallTravelAt(void* target, DWORD thread,
                     CampaignTravelAdmission admission) {
  if (!target || !admission || !thread || thread != GetCurrentThreadId() ||
      g_travel_stopping.load(std::memory_order_acquire)) return false;
  if (g_travel_hook.target)
    return g_travel_hook.target == target && g_travel_thread == thread &&
           g_admit_travel == admission;
  g_travel_thread = thread;
  g_admit_travel = admission;
  // This body is entered only by the game-thread campaign flow. Installation
  // happens outside original Tick, so no campaign stack is inside its prologue.
  if (!InstallInlineHook(&g_travel_hook, target,
          reinterpret_cast<void*>(&HookedNativeStartLevel), kStartLevelHead,
          sizeof(kStartLevelHead), &g_travel_original)) {
    g_travel_thread = 0;
    g_admit_travel = nullptr;
    return false;
  }
  return true;
}

bool ModuleCodeMatches(HMODULE module, uintptr_t rva,
                       const uint8_t* expected, size_t size) {
  auto* address = reinterpret_cast<uint8_t*>(module) + rva;
  MEMORY_BASIC_INFORMATION info = {};
  return VirtualQuery(address, &info, sizeof(info)) &&
      info.AllocationBase == module && info.State == MEM_COMMIT &&
      !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
      reinterpret_cast<uintptr_t>(address) + size <=
          reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize &&
      std::memcmp(address, expected, size) == 0;
}

}  // namespace

bool InstallNativeCampaignTravel(HMODULE executable, DWORD game_thread,
                                CampaignTravelAdmission admission) {
  if (!executable || executable != GetModuleHandleW(nullptr)) return false;
  auto* target = reinterpret_cast<uint8_t*>(executable) + kStartLevelRva;
  if (!g_travel_hook.target &&
      (!ModuleCodeMatches(executable, kStartLevelRva, kStartLevelHead, sizeof(kStartLevelHead)) ||
       !ModuleCodeMatches(executable, kStartLevelCallsiteRva, kStartLevelCallsite,
                          sizeof(kStartLevelCallsite)))) return false;
  return InstallTravelAt(target, game_thread, admission);
}

bool NativeCampaignTravelReady() {
  return g_travel_hook.target && g_travel_original && g_admit_travel &&
      !g_travel_stopping.load(std::memory_order_acquire);
}

bool ResumeNativeCampaignTravel(void* lobby) {
  if (!NativeCampaignTravelReady() || !lobby ||
      GetCurrentThreadId() != g_travel_thread || g_dispatching_travel) return false;
  auto entry = reinterpret_cast<StartLevelFn>(g_travel_hook.target);
  entry(static_cast<uint8_t*>(lobby) + kNativeScriptThisOffset);
  return true;
}

void StopNativeCampaignTravel() {
  // DLL is pinned by Proxy.cpp. Keep the trampoline/patch valid until process
  // exit: tearing down from the worker could race a game-thread callback.
  g_travel_stopping.store(true, std::memory_order_release);
}

}  // namespace wfcds
