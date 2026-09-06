// Production native binding + COMPLETE shipping bodies in executable memory.
// Metadata is a fixture; these tests do not run a game session or possession.
#include "NativePlayerInput.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>

using namespace wfcds;
extern "C" void WfcCallEsiVoid(void* function, void* script_this);
namespace {
struct Object {
  alignas(16) uint8_t bytes[0x800]{};
  const wchar_t* type;
  uint32_t index;
  bool valid = true, deleting = false;
};
Object player{{}, L"TnPlayerController", 1}, world{{}, L"WorldInfo", 2},
    other_world{{}, L"WorldInfo", 3}, defaults{{}, L"Defaults", 4};
Object* objects[] = {&player, &world, &other_world};
uint8_t* image = nullptr;
bool bad_offset = false, bad_read = false;
NativePlayerInputLease move_lease, look_lease;
Object* Lookup(void* p) {
  for (auto* object : objects) if (p == object->bytes && object->valid) return object;
  return nullptr;
}
bool Offset(void* p, const wchar_t* name, uint32_t* out) {
  auto* object = Lookup(p);
  if (!object || !name || !out) return false;
  if (object == &player) {
    if (!std::wcscmp(name, L"WorldInfo")) *out = 0x98;
    else if (!std::wcscmp(name, L"Role")) *out = 0x66;
    else if (!std::wcscmp(name, L"bIgnoreMoveInput")) *out = 0x305;
    else if (!std::wcscmp(name, L"bIgnoreLookInput")) *out = 0x306;
    else return false;
  } else if (!std::wcscmp(name, L"NetMode")) *out = 0x39C;
  else return false;
  return true;
}
void SetWorld(Object* value) {
  void* p = value ? value->bytes : nullptr;
  std::memcpy(player.bytes + 0x98, &p, 4);
}
void Reset() {
  for (auto* object : objects) {
    std::memset(object->bytes, 0, sizeof(object->bytes));
    object->valid = true; object->deleting = false;
  }
  player.index = 1; world.index = 2;
  SetWorld(&world); player.bytes[0x66] = 3;
  move_lease = {}; look_lease = {};
  g_input_owners[0] = g_input_owners[1] = nullptr;
  bad_offset = bad_read = false;
}
uint8_t* At(uintptr_t va) { return image + va - kInputImageBase; }
void AssertRejected() {
  const auto before = player;
  NativePlayerInputLease rejected;
  assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &rejected) == NativeInputResult::kRejected);
  assert(!rejected.held && !std::memcmp(before.bytes, player.bytes, sizeof(player.bytes)));
}
DWORD WINAPI WrongThread(void*) {
  AssertRejected();
  assert(!NativePlayerInputHeld(move_lease));
  assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kRejected);
  return 0;
}
void __attribute__((fastcall)) InterruptedMove(void* p, void*, uint32_t acquire) {
  AssertRejected(); // Reentry cannot acquire another axis or duplicate this one.
  NativePlayerInputLease other_axis;
  assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kLook, &other_axis) == NativeInputResult::kRejected);
  assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kRejected);
  reinterpret_cast<InputCall>(At(kIgnoreMoveVa))(p, nullptr, acquire);
  player.bytes[0x305] = 0; // Simulated reset after a call executed.
}
void VerifyFile(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  assert(file && file.tellg() == 37667328);
  std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
  file.seekg(0); assert(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
  assert(!std::memcmp(bytes.data() + kIgnoreLookVa - 0x10900C00,
      kIgnoreLookCode, sizeof(kIgnoreLookCode)));
  assert(!std::memcmp(bytes.data() + kIgnoreMoveVa - 0x10900C00,
      kIgnoreMoveCode.data(), kIgnoreMoveCode.size()));
  assert(!std::memcmp(bytes.data() + kInputResetVa - 0x10900C00,
      kInputResetCode, sizeof(kInputResetCode)));
  // Independent native evidence: ResetPlayerMovementInput copies default
  // byte counts, and ClientRestart reaches that reset. No name-only oracle.
  constexpr uint8_t reset_move[] = {0x8A,0x80,0x05,0x03,0x00,0x00,0x68,0x58,0x05,0x00,0x00,
                                  0x88,0x86,0xF5,0x02,0x00,0x00};
  bool found = false;
  for (size_t i = 0x117934E0 - 0x10900C00; i + sizeof(reset_move) <= 0x11793535 - 0x10900C00; ++i)
    if (!std::memcmp(bytes.data() + i, reset_move, sizeof(reset_move))) found = true;
  assert(found);
  std::puts("Native input: complete IgnoreMove/Look and ResetPlayerMovementInput bodies match TWFC.exe");
}
}  // namespace
namespace wfcds {
bool UnrealObjectIsA(void* p, const wchar_t* type) {
  auto* object = Lookup(p); return object && !std::wcscmp(object->type, type);
}
uint32_t UnrealObjectIndex(void* p) { auto* object = Lookup(p); return object ? object->index : 0; }
bool UnrealResolveObjectPropertyOffset(void* p, const wchar_t* name, uint32_t* out) {
  if (std::wcscmp(name, L"WorldInfo") || !Offset(p, name, out)) return false;
  if (bad_offset) ++*out;
  return true;
}
bool UnrealResolveBytePropertyOffset(void* p, const wchar_t* name, uint32_t* out) {
  if (!Offset(p, name, out)) return false;
  if (bad_offset) ++*out;
  return true;
}
bool UnrealReadObjectProperty(void* p, const wchar_t* name, void** out) {
  uint32_t offset = 0;
  if (bad_read || !Offset(p, name, &offset)) return false;
  std::memcpy(out, static_cast<uint8_t*>(p) + offset, 4);
  return !*out || Lookup(*out);
}
bool UnrealReadBoolProperty(void* p, const wchar_t* name, bool* out) {
  auto* object = Lookup(p);
  if (!object || std::wcscmp(name, L"bDeleteMe")) return false;
  *out = object->deleting; return true;
}
bool UnrealReadByteProperty(void* p, const wchar_t* name, uint8_t* out) {
  uint32_t offset = 0;
  if (bad_read || !Offset(p, name, &offset)) return false;
  *out = static_cast<uint8_t*>(p)[offset]; return true;
}
}  // namespace wfcds

int main(int argc, char** argv) {
  image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(image);
  std::memcpy(At(kIgnoreMoveVa), kIgnoreMoveCode.data(), kIgnoreMoveCode.size());
  std::memcpy(At(kIgnoreLookVa), kIgnoreLookCode, sizeof(kIgnoreLookCode));
  std::memcpy(At(kInputResetVa), kInputResetCode, sizeof(kInputResetCode));
  // Only the nested class-default resolver is a fixture. The full native
  // reset body, production inline hook, observer and ESI adapter all execute.
  auto* resolver = At(0x116EF210);
  resolver[0] = 0xB8;
  auto* default_data = defaults.bytes;
  std::memcpy(resolver + 1, &default_data, 4);
  resolver[5] = 0xC2; resolver[6] = 4; resolver[7] = 0;
  if (argc == 2) VerifyFile(argv[1]);
  DWORD old = 0;
  assert(VirtualProtect(image, 0x1000000, PAGE_EXECUTE_READWRITE, &old));
  FlushInstructionCache(GetCurrentProcess(), image, 0x1000000);
  const auto module = reinterpret_cast<HMODULE>(image);
  InputBindings bound;
  assert(BindInputImage(module, GetCurrentThreadId(), &bound));
  for (auto range : {std::pair<uintptr_t, size_t>{kIgnoreMoveVa, sizeof(kIgnoreLookCode)},
                    {kIgnoreLookVa, sizeof(kIgnoreLookCode)}, {kInputResetVa, sizeof(kInputResetCode)}}) {
    const auto va = range.first;
    for (size_t i = 0; i < range.second; ++i) {
      At(va)[i] ^= 1;
      InputBindings rejected;
      assert(!BindInputImage(module, GetCurrentThreadId(), &rejected));
      assert(!rejected.module);
      At(va)[i] ^= 1;
    }
  }
  assert(VirtualProtect(image, 0x1000000, PAGE_EXECUTE_READ, &old));
  FlushInstructionCache(GetCurrentProcess(), image, 0x1000000);
  g_input = bound; g_input.module = GetModuleHandleW(nullptr);
  Reset();
  assert(PrepareNativePlayerInput(GetModuleHandleW(nullptr), GetCurrentThreadId(), player.bytes));
  // Every legal baseline, both axes, repeated duplicate taps and independent
  // release order. All native calls execute real shipping instructions.
  for (unsigned n = 0; n < 5100; ++n) {
    Reset();
    player.bytes[0x305] = static_cast<uint8_t>(n % 255);
    player.bytes[0x306] = static_cast<uint8_t>((n * 7) % 255);
    const auto before = player;
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kApplied);
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kLook, &look_lease) == NativeInputResult::kApplied);
    assert(NativePlayerInputHeld(move_lease) && NativePlayerInputHeld(look_lease));
    AssertRejected();
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kRejected);
    auto copied = move_lease;
    assert(!NativePlayerInputHeld(copied));
    assert(NativeReleasePlayerInput(&copied) == NativeInputResult::kRejected);
    assert(NativeReleasePlayerInput(n & 1 ? &move_lease : &look_lease) == NativeInputResult::kApplied);
    assert(NativeReleasePlayerInput(n & 1 ? &look_lease : &move_lease) == NativeInputResult::kApplied);
    assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kRejected);
    assert(!move_lease.held && !look_lease.held);
    assert(!std::memcmp(before.bytes, player.bytes, sizeof(player.bytes)));
  }
  Reset(); player.bytes[0x305] = 255; AssertRejected();
  // Demonstrate the native overflow this module must prevent.
  bound.move(player.bytes + 16, nullptr, 1);
  assert(player.bytes[0x305] == 0);
  for (uint8_t mode = 1; mode < 4; ++mode) { Reset(); world.bytes[0x39C] = mode; AssertRejected(); }
  for (uint8_t role = 0; role < 3; ++role) { Reset(); player.bytes[0x66] = role; AssertRejected(); }
  Reset(); player.deleting = true; AssertRejected();
  Reset(); world.deleting = true; AssertRejected();
  Reset(); player.valid = false; AssertRejected();
  Reset(); SetWorld(nullptr); AssertRejected();
  Reset(); bad_offset = true; AssertRejected();
  Reset(); bad_read = true; AssertRejected();
  Reset();
  assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kApplied);
  const auto held = player;
  HANDLE thread = CreateThread(nullptr, 0, WrongThread, nullptr, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
  assert(!std::memcmp(held.bytes, player.bytes, sizeof(player.bytes)));
  // A native restart reset, foreign menu increment, stale object identity or
  // travel must NOT be balanced with a blind decrement or a fresh acquisition.
  for (unsigned failure = 0; failure < 5; ++failure) {
    Reset();
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kApplied);
    if (failure == 0) player.bytes[0x305] = 0;
    if (failure == 1) player.bytes[0x305] = 2;
    if (failure == 2) ++player.index;
    if (failure == 3) ++world.index;
    if (failure == 4) SetWorld(&other_world);
    const auto before = player;
    assert(!NativePlayerInputHeld(move_lease));
    assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kRejected);
    assert(move_lease.held && !std::memcmp(before.bytes, player.bytes, sizeof(player.bytes)));
    AssertRejected();
  }
  Reset(); g_input.move = &InterruptedMove;
  assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kStateMismatch);
  assert(move_lease.held && !NativePlayerInputHeld(move_lease));
  g_input.move = bound.move;
  Reset();
  player.bytes[0x305] = 1;
  assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kApplied);
  g_input.move = &InterruptedMove;
  assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kStateMismatch);
  assert(move_lease.held);
  g_input.move = bound.move;
  Reset();
  assert(NativeAcquirePlayerInput(player.bytes, static_cast<NativeInputAxis>(99), &move_lease) == NativeInputResult::kRejected);
  assert(NativeReleasePlayerInput(nullptr) == NativeInputResult::kRejected);
  // Same-value resets defeat a counter-only comparison. Invalidation remains
  // visible even when the engine resets to 1 or a cutscene re-adds one lock.
  for (unsigned n = 0; n < 3000; ++n) {
    Reset();
    defaults.bytes[0x305] = static_cast<uint8_t>(n % 3);
    defaults.bytes[0x306] = static_cast<uint8_t>((n + 1) % 3);
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kMove, &move_lease) == NativeInputResult::kApplied);
    assert(NativeAcquirePlayerInput(player.bytes, NativeInputAxis::kLook, &look_lease) == NativeInputResult::kApplied);
    alignas(16) uint8_t before_fpu[512]{}, after_fpu[512]{};
    auto* script_this = player.bytes + 16;
    auto* reset_entry = At(kInputResetVa);
    __asm__ volatile("fxsave %0\n\tpushl %3\n\tpushl %2\n\tcall _WfcCallEsiVoid\n\t"
                     "addl $8, %%esp\n\tfxsave %1"
                     : "=m"(before_fpu), "=m"(after_fpu)
                     : "r"(reset_entry), "r"(script_this) : "eax", "ecx", "edx", "memory", "cc");
    assert(!std::memcmp(before_fpu, after_fpu, sizeof(before_fpu)));
    assert(move_lease.invalidated && look_lease.invalidated);
    assert(player.bytes[0x305] == defaults.bytes[0x305] && player.bytes[0x306] == defaults.bytes[0x306]);
    player.bytes[0x305] = player.bytes[0x306] = 1;
    assert(!NativePlayerInputHeld(move_lease) && !NativePlayerInputHeld(look_lease));
    assert(NativeReleasePlayerInput(&move_lease) == NativeInputResult::kRejected);
    assert(NativeReleasePlayerInput(&look_lease) == NativeInputResult::kRejected);
    assert(player.bytes[0x305] == 1 && player.bytes[0x306] == 1);
  }
  Reset();
  defaults.bytes[0x305] = 3; defaults.bytes[0x306] = 4;
  WfcCallEsiVoid(At(kInputResetVa), player.bytes + 16); // No owned swap: ordinary reset.
  assert(player.bytes[0x305] == 3 && player.bytes[0x306] == 4);
  assert(!move_lease.invalidated && !look_lease.invalidated);
  StopNativePlayerInput();
  player.bytes[0x305] = player.bytes[0x306] = 0;
  WfcCallEsiVoid(At(kInputResetVa), player.bytes + 16); // Teardown remains pass-through.
  assert(player.bytes[0x305] == 3 && player.bytes[0x306] == 4);
  AssertRejected();
  // Local single-threaded fixture only; production keeps the pinned trampoline.
  assert(RemoveInlineHook(&g_input_reset_hook));
  WfcInputResetOriginal = nullptr;
  assert(VirtualFree(image, 0, MEM_RELEASE));
  std::puts("NativePlayerInputTests passed: 26,400 native counter calls, 3,000 hooked native resets, FPU/lease preservation");
}
