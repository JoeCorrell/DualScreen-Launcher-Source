// Exercise the real EAX-this hook/trampoline against a tiny native fixture.
// This is an ABI/admission test, not a substitute for campaign gameplay.
#include "NativeCampaignTravel.cpp"

#include <cassert>
#include <cstdio>

using namespace wfcds;
namespace {
uintptr_t observed_script_this = 0;
uint32_t starts = 0;
uint32_t admissions = 0;
bool allow = false;
bool recursive = false;
void* expected_lobby = nullptr;
bool Admit(void* lobby) {
  assert(lobby == expected_lobby);
  ++admissions;
  if (recursive) assert(!ResumeNativeCampaignTravel(lobby));
  return allow;
}
DWORD WINAPI WrongThread(void* lobby) {
  assert(!ResumeNativeCampaignTravel(lobby));
  assert(!InstallTravelAt(g_travel_hook.target, g_travel_thread, &Admit));
  return 0;
}
}

int main() {
  auto* body = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(body);
  std::memcpy(body, kStartLevelHead, sizeof(kStartLevelHead));
  // mov [observed_script_this],eax; inc dword [starts]; leave; ret
  body[9] = 0xA3;
  const auto context_address = reinterpret_cast<uintptr_t>(&observed_script_this);
  std::memcpy(body + 10, &context_address, 4);
  body[14] = 0xFF;
  body[15] = 0x05;
  const auto count_address = reinterpret_cast<uintptr_t>(&starts);
  std::memcpy(body + 16, &count_address, 4);
  body[20] = 0xC9;
  body[21] = 0xC3;
  DWORD previous = 0;
  assert(VirtualProtect(body, 64, PAGE_EXECUTE_READ, &previous));
  FlushInstructionCache(GetCurrentProcess(), body, 64);
  auto entry = reinterpret_cast<StartLevelFn>(body);
  uint8_t lobby[0x80] = {};
  expected_lobby = lobby;
  entry(lobby + 16);
  assert(starts == 1 && observed_script_this == reinterpret_cast<uintptr_t>(lobby + 16));
  assert(!InstallTravelAt(body + 1, GetCurrentThreadId(), &Admit));
  assert(body[0] == 0x55);
  assert(!InstallTravelAt(body, 0, &Admit));
  assert(InstallTravelAt(body, GetCurrentThreadId(), &Admit));
  assert(NativeCampaignTravelReady());
  assert(InstallTravelAt(body, GetCurrentThreadId(), &Admit));
  assert(!InstallNativeCampaignTravel(nullptr, GetCurrentThreadId(), &Admit));
  assert(!ResumeNativeCampaignTravel(nullptr));
  recursive = true;
  for (int n = 0; n < 1000; ++n) assert(ResumeNativeCampaignTravel(lobby));
  assert(starts == 1 && admissions == 1000);
  allow = true;
  assert(ResumeNativeCampaignTravel(lobby));
  assert(starts == 2 && admissions == 1001);
  assert(observed_script_this == reinterpret_cast<uintptr_t>(lobby + 16));
  // The stock caller supplies the script base directly; entry normalizes it.
  allow = false;
  entry(lobby + 16);
  assert(starts == 2 && admissions == 1002);
  HANDLE thread = CreateThread(nullptr, 0, &WrongThread, lobby, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  // Native null sentinel bypasses custom validation; don't subtract 0x10 and
  // manufacture a non-null UObject. The fixture original is safe for nulls.
  entry(reinterpret_cast<void*>(16));
  assert(starts == 3 && admissions == 1002);
  StopNativeCampaignTravel();
  assert(!NativeCampaignTravelReady());
  assert(!ResumeNativeCampaignTravel(lobby));
  entry(lobby + 16);  // Pinned stopping hook is a valid native pass-through.
  assert(starts == 4 && admissions == 1002);
  // Test-only cleanup: joined thread and no live native stack.
  assert(RemoveInlineHook(&g_travel_hook));
  assert(body[0] == 0x55);
  entry(lobby + 16);
  assert(starts == 5);
  assert(VirtualFree(body, 0, MEM_RELEASE));
  std::puts("NativeCampaignTravelTests passed");
}
