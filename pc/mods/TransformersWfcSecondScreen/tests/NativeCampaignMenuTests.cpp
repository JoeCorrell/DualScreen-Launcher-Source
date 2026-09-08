// Real x86 detour/trampoline ABI and retained-executable checks. The output
// fixture is an integer, not a substitute for engine FString or gameplay QA.
#include "NativeCampaignMenu.cpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace wfcds;
namespace {
uint32_t requests = 0;
bool substitute = false;
bool nested = false;
MenuGetter entry = nullptr;
MenuGetter custom_entry = nullptr;
MenuGetter playable_entry = nullptr;
uint32_t roster_requests = 0;
uint8_t binding[32] = {};
constexpr uint32_t kStock = 0x12345678u;
constexpr uint32_t kCustom = 0x76543210u;

bool Override(void* output) {
  ++requests;
  if (nested) {
    uint32_t value = 0;
    entry(nullptr, nullptr, nullptr, &value);
    assert(value == kStock);
  }
  if (!substitute) return false;
  *static_cast<uint32_t*>(output) = kCustom;
  return true;
}

void FilterRoster(void* context, void* output) {
  assert(context == binding);
  assert(*static_cast<uint32_t*>(output) == kStock);
  ++roster_requests;
  *static_cast<uint32_t*>(output) = kCustom;
  uint32_t nested_value = 0;
  playable_entry(binding + 16, nullptr, nullptr, &nested_value);
  assert(nested_value == kStock);
}

DWORD WINAPI WrongThread(void*) {
  assert(!InstallMenuAt(g_menu_hook.target, g_menu_thread, &Override));
  uint32_t value = 0;
  entry(nullptr, nullptr, nullptr, &value);
  assert(value == kStock);
  custom_entry(nullptr, nullptr, nullptr, &value);
  assert(value == kStock);
  playable_entry(binding + 16, nullptr, nullptr, &value);
  assert(value == kStock);
  return 0;
}
}

int main(int argc, char** argv) {
  assert(argc == 2);
  FILE* file = std::fopen(argv[1], "rb");
  assert(file && std::fseek(file, 0x10BEBD00u - 0x10900C00u, SEEK_SET) == 0);
  std::array<uint8_t, sizeof(kCurrentCharacterCode)> observed = {};
  assert(std::fread(observed.data(), 1, observed.size(), file) == observed.size());
  assert(CurrentCharacterCodeMatches(observed.data(), 0x10900000u));
  for (size_t index = 0; index < observed.size(); ++index) {
    observed[index] ^= 1;
    assert(!CurrentCharacterCodeMatches(observed.data(), 0x10900000u));
    observed[index] ^= 1;
  }
  for (size_t offset : kCurrentCharacterRelocations) {
    uint32_t address = 0;
    std::memcpy(&address, observed.data() + offset, sizeof(address));
    address += 0x21000000u - 0x10900000u;
    std::memcpy(observed.data() + offset, &address, sizeof(address));
  }
  assert(CurrentCharacterCodeMatches(observed.data(), 0x21000000u));
  assert(!CurrentCharacterCodeMatches(observed.data(), 0x10900000u));
  for (uintptr_t rva : {kCustomCharactersRva, kPlayableCharactersRva}) {
    assert(std::fseek(file, static_cast<long>(rva - 0xC00u), SEEK_SET) == 0);
    assert(std::fread(observed.data(), 1, observed.size(), file) == observed.size());
    assert(CurrentCharacterCodeMatches(observed.data(), 0x10900000u, rva));
    for (size_t index = 0; index < observed.size(); ++index) {
      observed[index] ^= 1;
      assert(!CurrentCharacterCodeMatches(observed.data(), 0x10900000u, rva));
      observed[index] ^= 1;
    }
  }
  std::fclose(file);

  auto* body = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(body);
  std::memcpy(body, kCurrentCharacterCode, 6);
  // mov eax,[esp+14]; mov dword [eax],kStock; add esp,0c; ret 8
  const uint8_t tail[] = {0x8B, 0x44, 0x24, 0x14, 0xC7, 0x00,
      0x78, 0x56, 0x34, 0x12, 0x83, 0xC4, 0x0C, 0xC2, 0x08, 0x00};
  std::memcpy(body + 6, tail, sizeof(tail));
  std::memcpy(body + 64, body, 6 + sizeof(tail));
  std::memcpy(body + 128, body, 6 + sizeof(tail));
  DWORD previous = 0;
  assert(VirtualProtect(body, 256, PAGE_EXECUTE_READ, &previous));
  assert(FlushInstructionCache(GetCurrentProcess(), body, 256));
  entry = reinterpret_cast<MenuGetter>(body);
  custom_entry = reinterpret_cast<MenuGetter>(body + 64);
  playable_entry = reinterpret_cast<MenuGetter>(body + 128);
  assert(!InstallNativeCampaignMenu(nullptr, GetCurrentThreadId(), &Override,
                                    &FilterRoster));
  assert(!InstallMenuAt(body, 0, &Override));
  assert(!InstallMenuAt(body, GetCurrentThreadId(), nullptr));
  assert(!InstallMenuAt(body + 1, GetCurrentThreadId(), &Override));
  assert(InstallMenuAt(body, GetCurrentThreadId(), &Override));
  assert(InstallMenuAt(body, GetCurrentThreadId(), &Override));
  assert(!NativeCampaignMenuReady());
  assert(InstallInlineHook(&g_menu_custom_hook, body + 64,
      reinterpret_cast<void*>(&HookedCustomCharacters), kCurrentCharacterCode,
      6, &g_menu_custom_original));
  assert(InstallInlineHook(&g_menu_playable_hook, body + 128,
      reinterpret_cast<void*>(&HookedPlayableCharacters), kCurrentCharacterCode,
      6, &g_menu_playable_original));
  g_menu_playable_characters = &FilterRoster;
  assert(NativeCampaignMenuReady());
  nested = true;
  for (uint32_t count = 0; count < 2000; ++count) {
    substitute = (count & 1) != 0;
    uint32_t value = 0;
    entry(nullptr, nullptr, nullptr, &value);
    assert(value == (substitute ? kCustom : kStock));
    custom_entry(nullptr, nullptr, nullptr, &value);
    assert(value == (substitute ? kCustom : kStock));
    playable_entry(binding + 16, nullptr, nullptr, &value);
    assert(value == kCustom);
    assert(requests == 2 * (count + 1));
    assert(roster_requests == count + 1);
  }
  HANDLE thread = CreateThread(nullptr, 0, &WrongThread, nullptr, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  assert(requests == 4000 && roster_requests == 2000);
  StopNativeCampaignMenu();
  assert(!NativeCampaignMenuReady());
  assert(!InstallMenuAt(body, GetCurrentThreadId(), &Override));
  uint32_t value = 0;
  entry(nullptr, nullptr, nullptr, &value);
  assert(value == kStock && requests == 4000);
  // No native stack remains in this fixture: test-only trampoline cleanup.
  assert(RemoveInlineHook(&g_menu_hook));
  assert(RemoveInlineHook(&g_menu_custom_hook));
  assert(RemoveInlineHook(&g_menu_playable_hook));
  assert(VirtualFree(body, 0, MEM_RELEASE));
  std::puts("NativeCampaignMenuTests passed");
}
