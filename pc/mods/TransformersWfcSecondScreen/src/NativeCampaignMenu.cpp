#include "NativeCampaignMenu.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "Hook.h"

namespace wfcds {
namespace {

// TnCharacterScriptBinding.GetCurrentCharacter's registered native wrapper,
// NOT the hidden-return-value body at 10BE8C30. The wrapper assigns into an
// initialized output FString; the body's output storage is uninitialized.
// Registration: 10A4A031..10A4A071, FName 12B30A50, wrapper 10BEBD00.
// ECX=compiled binding (unused), stack=FFrame*, FString*, RET 8.
constexpr uintptr_t kCurrentCharacterRva = 0x002EBD00u;
constexpr uintptr_t kCustomCharactersRva = 0x002EBBE0u;
constexpr uintptr_t kPlayableCharactersRva = 0x002EBC30u;
constexpr uint8_t kCurrentCharacterCode[] = {
    0x83, 0xEC, 0x0C, 0x8D, 0x04, 0x24, 0x50, 0xE8, 0x24, 0xCF, 0xFF, 0xFF,
    0x8B, 0x4C, 0x24, 0x14, 0x50, 0xE8, 0x3A, 0x54, 0xD1, 0xFF, 0x8B, 0x04,
    0x24, 0x33, 0xD2, 0x89, 0x54, 0x24, 0x08, 0x89, 0x54, 0x24, 0x04, 0x3B,
    0xC2, 0x74, 0x21, 0x8B, 0x0D, 0x04, 0x02, 0xB0, 0x12, 0x56, 0x8B, 0xF0,
    0x3B, 0xCA, 0x75, 0x0B, 0xE8, 0xE7, 0x73, 0x41, 0x00, 0x8B, 0x0D, 0x04,
    0x02, 0xB0, 0x12, 0x8B, 0x11, 0x8B, 0x42, 0x10, 0x56, 0xFF, 0xD0, 0x5E,
    0x83, 0xC4, 0x0C, 0xC2, 0x08, 0x00,
};
constexpr size_t kCurrentCharacterRelocations[] = {41, 59};
using MenuGetter = void (__attribute__((fastcall)) *)(
    void* compiled_binding, void* unused_edx, void* frame, void* result);
InlineHook g_menu_hook;
InlineHook g_menu_custom_hook;
InlineHook g_menu_playable_hook;
void* g_menu_original = nullptr;
void* g_menu_custom_original = nullptr;
void* g_menu_playable_original = nullptr;
DWORD g_menu_thread = 0;
CampaignMenuCurrentCharacter g_menu_current_character = nullptr;
CampaignMenuPlayableCharacters g_menu_playable_characters = nullptr;
std::atomic<bool> g_menu_stopping{false};
thread_local bool g_menu_dispatching = false;

void __attribute__((fastcall)) HookedCurrentCharacter(
    void* binding, void* unused_edx, void* frame, void* result) {
  const auto original = reinterpret_cast<MenuGetter>(g_menu_original);
  if (!original) return;  // Published before the executable branch.
  if (!g_menu_stopping.load(std::memory_order_acquire) &&
      GetCurrentThreadId() == g_menu_thread && g_menu_current_character &&
      result && !g_menu_dispatching) {
    struct Scope {
      Scope() { g_menu_dispatching = true; }
      ~Scope() { g_menu_dispatching = false; }
    } scope;
    if (g_menu_current_character(result)) return;
  }
  original(binding, unused_edx, frame, result);
}

void __attribute__((fastcall)) HookedCustomCharacters(
    void* binding, void* unused_edx, void* frame, void* result) {
  const auto original = reinterpret_cast<MenuGetter>(g_menu_custom_original);
  if (!original) return;
  if (!g_menu_stopping.load(std::memory_order_acquire) &&
      GetCurrentThreadId() == g_menu_thread && g_menu_current_character &&
      result && !g_menu_dispatching) {
    struct Scope {
      Scope() { g_menu_dispatching = true; }
      ~Scope() { g_menu_dispatching = false; }
    } scope;
    if (g_menu_current_character(result)) return;
  }
  original(binding, unused_edx, frame, result);
}

void __attribute__((fastcall)) HookedPlayableCharacters(
    void* binding, void* unused_edx, void* frame, void* result) {
  const auto original = reinterpret_cast<MenuGetter>(g_menu_playable_original);
  if (!original) return;
  original(binding, unused_edx, frame, result);
  const uintptr_t compiled = reinterpret_cast<uintptr_t>(binding);
  if (!g_menu_stopping.load(std::memory_order_acquire) &&
      GetCurrentThreadId() == g_menu_thread && g_menu_playable_characters &&
      result && compiled > 0x10 && !g_menu_dispatching) {
    struct Scope {
      Scope() { g_menu_dispatching = true; }
      ~Scope() { g_menu_dispatching = false; }
    } scope;
    g_menu_playable_characters(reinterpret_cast<void*>(compiled - 0x10), result);
  }
}

bool InstallMenuAt(void* target, DWORD thread,
                   CampaignMenuCurrentCharacter callback) {
  if (!target || !thread || thread != GetCurrentThreadId() || !callback ||
      g_menu_stopping.load(std::memory_order_acquire)) return false;
  if (g_menu_hook.target)
    return g_menu_hook.target == target && g_menu_thread == thread &&
           g_menu_current_character == callback;
  g_menu_thread = thread;
  g_menu_current_character = callback;
  if (!InstallInlineHook(&g_menu_hook, target,
          reinterpret_cast<void*>(&HookedCurrentCharacter),
          kCurrentCharacterCode, 6, &g_menu_original)) {
    g_menu_thread = 0;
    g_menu_current_character = nullptr;
    return false;
  }
  return true;
}

bool CurrentCharacterCodeMatches(const uint8_t* code, uint32_t module_base,
                                 uintptr_t rva = kCurrentCharacterRva) {
  if (!code) return false;
  uint32_t body = 0;
  if (rva == kCurrentCharacterRva) body = 0x002E8C30u;
  else if (rva == kCustomCharactersRva) body = 0x002E8690u;
  else if (rva == kPlayableCharactersRva) body = 0x002E87C0u;
  else return false;
  uint8_t expected[sizeof(kCurrentCharacterCode)];
  std::memcpy(expected, kCurrentCharacterCode, sizeof(expected));
  // All three registered wrappers have the identical FString lifetime body.
  // Only these three relative CALLs and two image relocations differ.
  const uint32_t calls[] = {body, 0x00001150u, 0x00703120u};
  const size_t offsets[] = {8, 18, 53};
  for (size_t index = 0; index < 3; ++index) {
    const uint32_t relative = calls[index] -
        static_cast<uint32_t>(rva + offsets[index] + 4);
    std::memcpy(expected + offsets[index], &relative, sizeof(relative));
  }
  for (size_t offset : kCurrentCharacterRelocations) {
    uint32_t address = 0;
    std::memcpy(&address, expected + offset, sizeof(address));
    address += module_base - 0x10900000u;
    std::memcpy(expected + offset, &address, sizeof(address));
  }
  return std::memcmp(code, expected, sizeof(expected)) == 0;
}

}  // namespace

bool InstallNativeCampaignMenu(HMODULE executable, DWORD game_thread,
                              CampaignMenuCurrentCharacter callback,
                              CampaignMenuPlayableCharacters playable) {
  if (!executable || executable != GetModuleHandleW(nullptr) || !playable ||
      !game_thread || game_thread != GetCurrentThreadId()) return false;
  const uintptr_t rvas[] = {kCurrentCharacterRva, kCustomCharactersRva,
                            kPlayableCharactersRva};
  InlineHook* hooks[] = {&g_menu_hook, &g_menu_custom_hook, &g_menu_playable_hook};
  for (size_t index = 0; index < 3; ++index) {
    auto* target = reinterpret_cast<uint8_t*>(executable) + rvas[index];
    if (hooks[index]->target) {
      if (hooks[index]->target != target) return false;
      continue;
    }
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(target, &info, sizeof(info)) ||
        info.AllocationBase != executable || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        reinterpret_cast<uintptr_t>(target) + sizeof(kCurrentCharacterCode) >
            reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize ||
        !CurrentCharacterCodeMatches(target,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(executable)),
            rvas[index]))
      return false;
  }
  if (g_menu_playable_characters && g_menu_playable_characters != playable)
    return false;
  if (!InstallMenuAt(reinterpret_cast<uint8_t*>(executable) +
          kCurrentCharacterRva, game_thread, callback)) return false;
  g_menu_playable_characters = playable;
  if (!g_menu_custom_hook.target && !InstallInlineHook(&g_menu_custom_hook,
          reinterpret_cast<uint8_t*>(executable) + kCustomCharactersRva,
          reinterpret_cast<void*>(&HookedCustomCharacters), kCurrentCharacterCode,
          6, &g_menu_custom_original)) return false;
  if (!g_menu_playable_hook.target && !InstallInlineHook(&g_menu_playable_hook,
          reinterpret_cast<uint8_t*>(executable) + kPlayableCharactersRva,
          reinterpret_cast<void*>(&HookedPlayableCharacters), kCurrentCharacterCode,
          6, &g_menu_playable_original)) return false;
  return NativeCampaignMenuReady();
}

bool NativeCampaignMenuReady() {
  return g_menu_hook.target && g_menu_original && g_menu_current_character &&
         g_menu_custom_hook.target && g_menu_custom_original &&
         g_menu_playable_hook.target && g_menu_playable_original &&
         g_menu_playable_characters &&
         !g_menu_stopping.load(std::memory_order_acquire);
}

void StopNativeCampaignMenu() {
  g_menu_stopping.store(true, std::memory_order_release);
}

}  // namespace wfcds
