#include "CombatLagFix.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "Log.h"

namespace fo3ds {
namespace {

constexpr char kSupportedSha256[] =
    "c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e";

// Fallout 3 1.7.0.4 image-relative addresses. These are deliberately not
// signatures: the complete executable hash and the surrounding instruction
// bytes are both checked before a write is attempted.
constexpr uintptr_t kHudFrameTimerCallRva = 0x0052DA40;
constexpr uintptr_t kTargetHealthTimerCallRva = 0x005309DF;
constexpr uintptr_t kOriginalTimerUpdateRva = 0x005DA660;
constexpr uintptr_t kGlobalTimeScaleRva = 0x00D16708;
constexpr uintptr_t kWaitForSingleObjectIatRva = 0x00B2407C;
constexpr uintptr_t kWaitForMultipleObjectsIatRva = 0x00B24100;
constexpr uintptr_t kWaitForSingleObjectExIatRva = 0x00B2417C;
constexpr uintptr_t kSleepIatRva = 0x00B241E8;

constexpr uint8_t kHudFrameContext[] = {
    0xFF, 0x15, 0x58, 0x40, 0xF2, 0x00,  // call GetTickCount
    0x50,                                // push eax
    0x8D, 0x8F, 0xF4, 0x01, 0x00, 0x00,  // lea ecx,[edi+1F4h]
    0xE8, 0x1B, 0xCC, 0x0A, 0x00};       // call timer update
constexpr uint8_t kTargetHealthContext[] = {
    0xFF, 0x15, 0x58, 0x40, 0xF2, 0x00,  // call GetTickCount
    0x50,                                // push eax
    0x8D, 0x8E, 0xF4, 0x01, 0x00, 0x00,  // lea ecx,[esi+1F4h]
    0xE8, 0x7C, 0x9C, 0x0A, 0x00};       // call timer update
constexpr size_t kCallOffsetInContext = 13;

#pragma pack(push, 1)
struct GameTimerLayout {
  uint8_t paused;
  uint8_t pad01[3];
  float alternate_clock;
  float accumulator;
  float delta_seconds;
  uint32_t previous_tick;
  uint32_t origin_tick;
  uint8_t pad18;
  uint8_t repeated_tick;
};
#pragma pack(pop)

static_assert(offsetof(GameTimerLayout, delta_seconds) == 0x0C,
              "unexpected Fallout timer layout");
static_assert(offsetof(GameTimerLayout, previous_tick) == 0x10,
              "unexpected Fallout timer layout");
static_assert(offsetof(GameTimerLayout, origin_tick) == 0x14,
              "unexpected Fallout timer layout");
static_assert(offsetof(GameTimerLayout, repeated_tick) == 0x19,
              "unexpected Fallout timer layout");

uintptr_t g_image_base = 0;
std::atomic<uint32_t> g_hud_calls{0};
std::atomic<uint32_t> g_health_change_calls{0};
std::atomic<uint32_t> g_alternate_calls{0};
std::atomic<uint32_t> g_paused_calls{0};
std::atomic<uint32_t> g_hud_last_tick{0};
std::atomic<uint32_t> g_hud_max_gap_ms{0};
std::atomic<uint32_t> g_patch_active{0};
uint8_t* g_hud_call = nullptr;
uint8_t* g_health_call = nullptr;
uint8_t g_hud_replacement[5] = {};
uint8_t g_health_replacement[5] = {};
uint64_t g_next_diagnostic_ms = 0;
DWORD g_game_thread_id = 0;

enum class WaitKind : uint32_t {
  kNone = 0,
  kSleep = 1,
  kSingle = 2,
  kSingleEx = 3,
  kMultiple = 4,
};

struct WaitSite {
  std::atomic<uint32_t> caller{0};
  std::atomic<uint32_t> kind{0};
  std::atomic<uint32_t> calls{0};
  std::atomic<uint32_t> total_ms{0};
  std::atomic<uint32_t> max_ms{0};
  std::atomic<uint32_t> requested_ms{0};
};

constexpr size_t kWaitSiteCount = 24;
WaitSite g_wait_sites[kWaitSiteCount];
std::atomic<uint32_t> g_wait_diagnostics_active{0};
std::atomic<uint32_t> g_active_wait_kind{0};
std::atomic<uint32_t> g_active_wait_caller{0};
std::atomic<uint32_t> g_active_wait_requested_ms{0};
std::atomic<uint32_t> g_active_wait_since{0};

using SleepFunction = void(WINAPI*)(DWORD);
using WaitSingleFunction = DWORD(WINAPI*)(HANDLE, DWORD);
using WaitSingleExFunction = DWORD(WINAPI*)(HANDLE, DWORD, BOOL);
using WaitMultipleFunction = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD);
SleepFunction g_original_sleep = nullptr;
WaitSingleFunction g_original_wait_single = nullptr;
WaitSingleExFunction g_original_wait_single_ex = nullptr;
WaitMultipleFunction g_original_wait_multiple = nullptr;

// GCC's 32-bit fastcall ABI gives us the same ECX + stack layout as the game's
// thiscall when a deliberately ignored EDX parameter occupies register slot 2.
using OriginalTimerUpdate = void(__attribute__((fastcall)) *)(
    GameTimerLayout*, void*, uint32_t);

// The stock timer forces every sub-10 ms HUD update to Sleep(10-elapsed) and
// then reports a fabricated 10 ms delta. That timer is correct for deliberate
// frame pacing elsewhere, but these two health-bar call sites use it on the
// main game thread by mistake. Preserve the timer's bookkeeping while using
// the real elapsed time and never waiting.
void UpdateTargetHealthTimerNoWait(GameTimerLayout* timer,
                                  uint32_t current_tick) {
  if (!timer || !g_image_base) return;

  // The alternate-clock branch never executes the offending Sleep and has
  // additional accumulator semantics. Leave that uncommon mode untouched.
  if (timer->alternate_clock != 0.0f) {
    g_alternate_calls.fetch_add(1, std::memory_order_relaxed);
    auto original = reinterpret_cast<OriginalTimerUpdate>(
        g_image_base + kOriginalTimerUpdateRva);
    original(timer, nullptr, current_tick);
    return;
  }

  if (timer->paused) {
    g_paused_calls.fetch_add(1, std::memory_order_relaxed);
    timer->delta_seconds = 0.0f;
    return;
  }

  const uint32_t relative_tick = current_tick - timer->origin_tick;
  uint32_t elapsed = relative_tick - timer->previous_tick;
  if (elapsed == 0) {
    timer->repeated_tick = 1;
    elapsed = 1;
  } else {
    timer->repeated_tick = 0;
  }
  if (elapsed > 166) elapsed = 166;

  const auto* time_scale = reinterpret_cast<const volatile float*>(
      g_image_base + kGlobalTimeScaleRva);
  timer->delta_seconds = static_cast<float>(elapsed) * 0.001f * *time_scale;
  timer->previous_tick = relative_tick;
}

void RecordHudCall(uint32_t current_tick) {
  g_hud_calls.fetch_add(1, std::memory_order_relaxed);
  const uint32_t previous =
      g_hud_last_tick.exchange(current_tick, std::memory_order_relaxed);
  if (!previous) return;
  const uint32_t gap = current_tick - previous;
  uint32_t maximum = g_hud_max_gap_ms.load(std::memory_order_relaxed);
  while (gap > maximum &&
         !g_hud_max_gap_ms.compare_exchange_weak(
             maximum, gap, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

// Separate wrappers prove which stock call site is active while retaining the
// game's thiscall-compatible ECX/stack layout.
void __attribute__((fastcall)) UpdateHudHealthTimerNoWait(
    GameTimerLayout* timer, void*, uint32_t current_tick) {
  RecordHudCall(current_tick);
  UpdateTargetHealthTimerNoWait(timer, current_tick);
}

void __attribute__((fastcall)) UpdateChangedHealthTimerNoWait(
    GameTimerLayout* timer, void*, uint32_t current_tick) {
  g_health_change_calls.fetch_add(1, std::memory_order_relaxed);
  UpdateTargetHealthTimerNoWait(timer, current_tick);
}

bool BeginTrackedWait(WaitKind kind, uint32_t caller, uint32_t requested_ms,
                      uint32_t* started) {
  if (!g_wait_diagnostics_active.load(std::memory_order_acquire)) return false;
  if (GetCurrentThreadId() != g_game_thread_id) return false;
  const uint32_t now = GetTickCount();
  if (started) *started = now;
  g_active_wait_caller.store(caller, std::memory_order_relaxed);
  g_active_wait_requested_ms.store(requested_ms, std::memory_order_relaxed);
  g_active_wait_since.store(now, std::memory_order_relaxed);
  g_active_wait_kind.store(static_cast<uint32_t>(kind),
                           std::memory_order_release);
  return true;
}

void RecordTrackedWait(WaitKind kind, uint32_t caller, uint32_t requested_ms,
                       uint32_t started) {
  const uint32_t elapsed = GetTickCount() - started;
  for (WaitSite& site : g_wait_sites) {
    uint32_t owner = site.caller.load(std::memory_order_relaxed);
    if (owner != caller) {
      if (owner != 0 || !site.caller.compare_exchange_strong(
                            owner, caller, std::memory_order_relaxed,
                            std::memory_order_relaxed))
        continue;
    }
    site.kind.store(static_cast<uint32_t>(kind), std::memory_order_relaxed);
    site.requested_ms.store(requested_ms, std::memory_order_relaxed);
    site.calls.fetch_add(1, std::memory_order_relaxed);
    site.total_ms.fetch_add(elapsed, std::memory_order_relaxed);
    uint32_t maximum = site.max_ms.load(std::memory_order_relaxed);
    while (elapsed > maximum &&
           !site.max_ms.compare_exchange_weak(
               maximum, elapsed, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    break;
  }
  g_active_wait_kind.store(0, std::memory_order_release);
}

void WINAPI TrackedSleep(DWORD milliseconds) {
  const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
      __builtin_return_address(0)));
  uint32_t started = 0;
  const bool tracked =
      BeginTrackedWait(WaitKind::kSleep, caller, milliseconds, &started);
  g_original_sleep(milliseconds);
  if (tracked)
    RecordTrackedWait(WaitKind::kSleep, caller, milliseconds, started);
}

DWORD WINAPI TrackedWaitForSingleObject(HANDLE handle, DWORD milliseconds) {
  const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
      __builtin_return_address(0)));
  uint32_t started = 0;
  const bool tracked =
      BeginTrackedWait(WaitKind::kSingle, caller, milliseconds, &started);
  const DWORD result = g_original_wait_single(handle, milliseconds);
  if (tracked)
    RecordTrackedWait(WaitKind::kSingle, caller, milliseconds, started);
  return result;
}

DWORD WINAPI TrackedWaitForSingleObjectEx(HANDLE handle, DWORD milliseconds,
                                          BOOL alertable) {
  const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
      __builtin_return_address(0)));
  uint32_t started = 0;
  const bool tracked =
      BeginTrackedWait(WaitKind::kSingleEx, caller, milliseconds, &started);
  const DWORD result =
      g_original_wait_single_ex(handle, milliseconds, alertable);
  if (tracked)
    RecordTrackedWait(WaitKind::kSingleEx, caller, milliseconds, started);
  return result;
}

DWORD WINAPI TrackedWaitForMultipleObjects(DWORD count, const HANDLE* handles,
                                           BOOL wait_all,
                                           DWORD milliseconds) {
  const uint32_t caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
      __builtin_return_address(0)));
  uint32_t started = 0;
  const bool tracked =
      BeginTrackedWait(WaitKind::kMultiple, caller, milliseconds, &started);
  const DWORD result =
      g_original_wait_multiple(count, handles, wait_all, milliseconds);
  if (tracked)
    RecordTrackedWait(WaitKind::kMultiple, caller, milliseconds, started);
  return result;
}

template <typename Function>
bool ReplaceImport(const BuildIdentity& build, uintptr_t slot_rva,
                   const char* name, Function replacement,
                   Function* original) {
  if (!original) return false;
  auto* slot = reinterpret_cast<Function*>(g_image_base + slot_rva);
  if (!build.image.Contains(reinterpret_cast<uintptr_t>(slot), sizeof(*slot)))
    return false;
  HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  const FARPROC procedure = kernel ? GetProcAddress(kernel, name) : nullptr;
  Function expected = nullptr;
  static_assert(sizeof(expected) == sizeof(procedure),
                "Win32 function pointer size mismatch");
  std::memcpy(&expected, &procedure, sizeof(expected));
  Function current = nullptr;
  std::memcpy(&current, slot, sizeof(current));
  if (!expected || current != expected) {
    Log(LogLevel::kWarning,
        "[COMBAT-PERF] wait diagnostic import mismatch name=%s slot=%p current=%08lX expected=%08lX",
        name, slot,
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(current)),
        static_cast<unsigned long>(reinterpret_cast<uintptr_t>(expected)));
    return false;
  }
  DWORD protection = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
    return false;
  *original = current;
  std::memcpy(slot, &replacement, sizeof(replacement));
  DWORD ignored = 0;
  const bool restored =
      VirtualProtect(slot, sizeof(*slot), protection, &ignored) != FALSE;
  Function verified = nullptr;
  std::memcpy(&verified, slot, sizeof(verified));
  return restored && verified == replacement;
}

template <typename Function>
bool RestoreImport(const BuildIdentity& build, uintptr_t slot_rva,
                   Function replacement, Function original) {
  if (!original) return false;
  auto* slot = reinterpret_cast<Function*>(g_image_base + slot_rva);
  if (!build.image.Contains(reinterpret_cast<uintptr_t>(slot), sizeof(*slot)))
    return false;
  Function current = nullptr;
  std::memcpy(&current, slot, sizeof(current));
  if (current == original) return true;
  if (current != replacement) return false;
  DWORD protection = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
    return false;
  std::memcpy(slot, &original, sizeof(original));
  DWORD ignored = 0;
  const bool restored =
      VirtualProtect(slot, sizeof(*slot), protection, &ignored) != FALSE;
  Function verified = nullptr;
  std::memcpy(&verified, slot, sizeof(verified));
  return restored && verified == original;
}

void InstallWaitDiagnostics(const BuildIdentity& build) {
  g_wait_diagnostics_active.store(0, std::memory_order_release);
  g_game_thread_id = GetCurrentThreadId();
  const bool sleep_ok = ReplaceImport(
      build, kSleepIatRva, "Sleep", &TrackedSleep, &g_original_sleep);
  const bool single_ok = ReplaceImport(
      build, kWaitForSingleObjectIatRva, "WaitForSingleObject",
      &TrackedWaitForSingleObject, &g_original_wait_single);
  const bool single_ex_ok = ReplaceImport(
      build, kWaitForSingleObjectExIatRva, "WaitForSingleObjectEx",
      &TrackedWaitForSingleObjectEx, &g_original_wait_single_ex);
  const bool multiple_ok = ReplaceImport(
      build, kWaitForMultipleObjectsIatRva, "WaitForMultipleObjects",
      &TrackedWaitForMultipleObjects, &g_original_wait_multiple);
  const bool active = sleep_ok && single_ok && single_ex_ok && multiple_ok;
  if (!active) {
    // An optional diagnostic must never leave a partly intercepted import
    // table. Roll back every slot that was successfully committed.
    const bool multiple_rolled_back = !multiple_ok || RestoreImport(
        build, kWaitForMultipleObjectsIatRva,
        &TrackedWaitForMultipleObjects, g_original_wait_multiple);
    const bool single_ex_rolled_back = !single_ex_ok || RestoreImport(
        build, kWaitForSingleObjectExIatRva,
        &TrackedWaitForSingleObjectEx, g_original_wait_single_ex);
    const bool single_rolled_back = !single_ok || RestoreImport(
        build, kWaitForSingleObjectIatRva,
        &TrackedWaitForSingleObject, g_original_wait_single);
    const bool sleep_rolled_back = !sleep_ok || RestoreImport(
        build, kSleepIatRva, &TrackedSleep, g_original_sleep);
    Log(LogLevel::kWarning,
        "[COMBAT-PERF] incomplete wait tracing rolled back sleep=%u single=%u single_ex=%u multiple=%u",
        sleep_rolled_back ? 1u : 0u, single_rolled_back ? 1u : 0u,
        single_ex_rolled_back ? 1u : 0u,
        multiple_rolled_back ? 1u : 0u);
  }
  g_wait_diagnostics_active.store(active ? 1u : 0u,
                                  std::memory_order_release);
  Log(active ? LogLevel::kInfo : LogLevel::kWarning,
      "[COMBAT-PERF] main-thread wait tracing active=%u thread=%lu sleep=%u single=%u single_ex=%u multiple=%u",
      active ? 1u : 0u, static_cast<unsigned long>(g_game_thread_id),
      sleep_ok ? 1u : 0u, single_ok ? 1u : 0u,
      single_ex_ok ? 1u : 0u, multiple_ok ? 1u : 0u);
}

struct PreparedCall {
  uint8_t* address = nullptr;
  uint8_t original[5] = {};
  uint8_t replacement[5] = {};
  DWORD old_protection = 0;
};

bool PrepareRelativeCall(uint8_t* context, const uint8_t* expected,
                         size_t expected_size, const char* label,
                         uintptr_t replacement_target,
                         PreparedCall* prepared) {
  if (!prepared) return false;
  if (std::memcmp(context, expected, expected_size) != 0) {
    uint8_t* call = context + kCallOffsetInContext;
    if (call[0] == 0xE8) {
      int32_t current_rel32 = 0;
      std::memcpy(&current_rel32, call + 1, sizeof(current_rel32));
      const uintptr_t current_target = reinterpret_cast<uintptr_t>(call + 5) +
          static_cast<intptr_t>(current_rel32);
      if (current_target == replacement_target) {
        prepared->address = call;
        std::memcpy(prepared->original, call, sizeof(prepared->original));
        std::memcpy(prepared->replacement, call, sizeof(prepared->replacement));
        return true;
      }
    }
    Log(LogLevel::kWarning,
        "[COMBAT-PERF] %s context mismatch; no timing code changed", label);
    return false;
  }

  prepared->address = context + kCallOffsetInContext;
  std::memcpy(prepared->original, prepared->address,
              sizeof(prepared->original));
  const int64_t displacement =
      static_cast<int64_t>(replacement_target) -
      static_cast<int64_t>(reinterpret_cast<uintptr_t>(prepared->address + 5));
  if (displacement < INT32_MIN || displacement > INT32_MAX) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] %s replacement is outside rel32 range", label);
    return false;
  }

  prepared->replacement[0] = 0xE8;
  const int32_t rel32 = static_cast<int32_t>(displacement);
  std::memcpy(prepared->replacement + 1, &rel32, sizeof(rel32));
  return true;
}

}  // namespace

bool ApplyCombatLagFix(const BuildIdentity& build) {
  if (!build.valid_pe || !build.is_i386 || !build.known_candidate ||
      std::strcmp(build.file_sha256, kSupportedSha256) != 0) {
    Log(LogLevel::kWarning,
        "[COMBAT-PERF] unsupported executable; health-bar timing fix disabled");
    return false;
  }

  g_image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!g_image_base) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] executable image unavailable; timing fix disabled");
    return false;
  }

  auto* frame_context =
      reinterpret_cast<uint8_t*>(g_image_base + kHudFrameTimerCallRva -
                                 kCallOffsetInContext);
  auto* health_context =
      reinterpret_cast<uint8_t*>(g_image_base + kTargetHealthTimerCallRva -
                                 kCallOffsetInContext);
  const uintptr_t frame_address = reinterpret_cast<uintptr_t>(frame_context);
  const uintptr_t health_address = reinterpret_cast<uintptr_t>(health_context);
  const uintptr_t scale_address = g_image_base + kGlobalTimeScaleRva;
  if (!build.text.Contains(frame_address, sizeof(kHudFrameContext)) ||
      !build.text.Contains(health_address, sizeof(kTargetHealthContext)) ||
      !build.data.Contains(scale_address, sizeof(float))) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] patch addresses are outside verified PE sections");
    return false;
  }
  float time_scale = 0.0f;
  std::memcpy(&time_scale, reinterpret_cast<void*>(scale_address),
              sizeof(time_scale));
  if (!std::isfinite(time_scale) || time_scale < 0.0f || time_scale > 100.0f) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] invalid game time-scale global; patch disabled");
    return false;
  }

  PreparedCall frame = {};
  PreparedCall health = {};
  const bool frame_ok = PrepareRelativeCall(
      frame_context, kHudFrameContext, sizeof(kHudFrameContext), "hud-frame",
      reinterpret_cast<uintptr_t>(&UpdateHudHealthTimerNoWait), &frame);
  const bool health_ok = PrepareRelativeCall(
      health_context, kTargetHealthContext, sizeof(kTargetHealthContext),
      "target-health-change",
      reinterpret_cast<uintptr_t>(&UpdateChangedHealthTimerNoWait), &health);
  if (!frame_ok || !health_ok) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] incomplete validation; fix inactive frame=%u health=%u",
        frame_ok ? 1u : 0u, health_ok ? 1u : 0u);
    return false;
  }

  // Make both sites writable before changing either one. This keeps the
  // operation transactional: a protection failure cannot leave only half of
  // the fix active.
  if (!VirtualProtect(frame.address, sizeof(frame.replacement),
                      PAGE_EXECUTE_READWRITE, &frame.old_protection)) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] hud-frame VirtualProtect failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  if (!VirtualProtect(health.address, sizeof(health.replacement),
                      PAGE_EXECUTE_READWRITE, &health.old_protection)) {
    const DWORD error = GetLastError();
    DWORD ignored = 0;
    VirtualProtect(frame.address, sizeof(frame.replacement),
                   frame.old_protection, &ignored);
    Log(LogLevel::kError,
        "[COMBAT-PERF] target-health-change VirtualProtect failed error=%lu; no bytes changed",
        static_cast<unsigned long>(error));
    return false;
  }

  std::memcpy(frame.address, frame.replacement, sizeof(frame.replacement));
  std::memcpy(health.address, health.replacement, sizeof(health.replacement));
  const bool flushed =
      FlushInstructionCache(GetCurrentProcess(), frame.address,
                            sizeof(frame.replacement)) != FALSE &&
      FlushInstructionCache(GetCurrentProcess(), health.address,
                            sizeof(health.replacement)) != FALSE;
  if (!flushed) {
    std::memcpy(frame.address, frame.original, sizeof(frame.original));
    std::memcpy(health.address, health.original, sizeof(health.original));
    FlushInstructionCache(GetCurrentProcess(), frame.address,
                          sizeof(frame.original));
    FlushInstructionCache(GetCurrentProcess(), health.address,
                          sizeof(health.original));
  }
  DWORD ignored = 0;
  const bool health_restored =
      VirtualProtect(health.address, sizeof(health.replacement),
                     health.old_protection, &ignored) != FALSE;
  const bool frame_restored =
      VirtualProtect(frame.address, sizeof(frame.replacement),
                     frame.old_protection, &ignored) != FALSE;
  if (!flushed || !health_restored || !frame_restored) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] patch commit failed flush=%u frame_protect=%u health_protect=%u",
        flushed ? 1u : 0u, frame_restored ? 1u : 0u,
        health_restored ? 1u : 0u);
    return false;
  }
  if (std::memcmp(frame.address, frame.replacement,
                  sizeof(frame.replacement)) != 0 ||
      std::memcmp(health.address, health.replacement,
                  sizeof(health.replacement)) != 0) {
    Log(LogLevel::kError,
        "[COMBAT-PERF] patch verification failed after commit");
    return false;
  }

  g_hud_call = frame.address;
  g_health_call = health.address;
  std::memcpy(g_hud_replacement, frame.replacement,
              sizeof(g_hud_replacement));
  std::memcpy(g_health_replacement, health.replacement,
              sizeof(g_health_replacement));
  g_patch_active.store(1, std::memory_order_release);
  g_next_diagnostic_ms = GetTickCount64() + 10000;
  InstallWaitDiagnostics(build);

  Log(LogLevel::kInfo,
      "[COMBAT-PERF] hud-frame health-bar wait removed call=%p target=%p",
      frame.address, &UpdateHudHealthTimerNoWait);
  Log(LogLevel::kInfo,
      "[COMBAT-PERF] target-health-change wait removed call=%p target=%p",
      health.address, &UpdateChangedHealthTimerNoWait);

  Log(LogLevel::kInfo,
      "[COMBAT-PERF] exact-build fix active: main thread no longer waits on target health UI");
  return true;
}

void FlushCombatLagDiagnostics() {
  if (!g_patch_active.load(std::memory_order_acquire)) return;
  const uint64_t now = GetTickCount64();
  if (now < g_next_diagnostic_ms) return;
  g_next_diagnostic_ms = now + 10000;

  const uint32_t hud_calls =
      g_hud_calls.exchange(0, std::memory_order_relaxed);
  const uint32_t health_calls =
      g_health_change_calls.exchange(0, std::memory_order_relaxed);
  const uint32_t alternate_calls =
      g_alternate_calls.exchange(0, std::memory_order_relaxed);
  const uint32_t paused_calls =
      g_paused_calls.exchange(0, std::memory_order_relaxed);
  const uint32_t max_gap =
      g_hud_max_gap_ms.exchange(0, std::memory_order_relaxed);
  const bool bytes_intact = g_hud_call && g_health_call &&
      std::memcmp(g_hud_call, g_hud_replacement,
                  sizeof(g_hud_replacement)) == 0 &&
      std::memcmp(g_health_call, g_health_replacement,
                  sizeof(g_health_replacement)) == 0;

  Log(LogLevel::kInfo,
      "[COMBAT-PERF] runtime window_ms=10000 hud_calls=%u health_changes=%u alternate=%u paused=%u hud_max_gap_ms=%u patch_bytes=%u",
      hud_calls, health_calls, alternate_calls, paused_calls, max_gap,
      bytes_intact ? 1u : 0u);

  if (!g_wait_diagnostics_active.load(std::memory_order_acquire)) return;
  const uint32_t active_kind =
      g_active_wait_kind.load(std::memory_order_acquire);
  if (active_kind != 0) {
    const uint32_t active_since =
        g_active_wait_since.load(std::memory_order_relaxed);
    Log(LogLevel::kInfo,
        "[COMBAT-WAIT] active kind=%u caller=%08X requested_ms=%u elapsed_ms=%u",
        active_kind, g_active_wait_caller.load(std::memory_order_relaxed),
        g_active_wait_requested_ms.load(std::memory_order_relaxed),
        GetTickCount() - active_since);
  }
  for (WaitSite& site : g_wait_sites) {
    const uint32_t calls = site.calls.exchange(0, std::memory_order_relaxed);
    const uint32_t total_ms =
        site.total_ms.exchange(0, std::memory_order_relaxed);
    const uint32_t max_ms =
        site.max_ms.exchange(0, std::memory_order_relaxed);
    if (!calls && !total_ms && !max_ms) continue;
    Log(LogLevel::kInfo,
        "[COMBAT-WAIT] kind=%u caller=%08X calls=%u total_ms=%u max_ms=%u requested_ms=%u",
        site.kind.load(std::memory_order_relaxed),
        site.caller.load(std::memory_order_relaxed), calls, total_ms, max_ms,
        site.requested_ms.load(std::memory_order_relaxed));
  }
}

}  // namespace fo3ds
