// Local x86 fixtures only. Executes shipping CancelMove and PoppedState bodies;
// the native engine's state-stack machinery and GC callback are test doubles.
#include "NativeAiMovement.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>

using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x500] = {}; bool valid = true; };
Object controller, world, state_node;
alignas(4) uint8_t frame[0x54] = {};
void* state_stack[3] = {state_node.bytes, nullptr, nullptr};
uint8_t* test_image = nullptr;
bool deleting = false, delegates_missing = false, wrong_offset = false;
bool ignore_pop = false, preserve_callbacks = false, pop_reentry = false;
uint8_t net_mode = 0;
unsigned pops = 0;
uintptr_t Read(const void* p, size_t offset) {
  uintptr_t result = 0; std::memcpy(&result, static_cast<const uint8_t*>(p) + offset, 4); return result;
}
void Put(void* p, size_t offset, uintptr_t v) { std::memcpy(static_cast<uint8_t*>(p) + offset, &v, 4); }
uint8_t* At(uintptr_t va) { return test_image + va - kMovementImageBase; }
void __attribute__((stdcall)) PopFixture(void* object, uint32_t all) {
  assert(object == controller.bytes && all == 0);
  ++pops;
  if (pop_reentry) assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kRejected);
  if (ignore_pop) return;
  const auto* table = reinterpret_cast<const uintptr_t*>(Read(controller.bytes, 16));
  reinterpret_cast<MoveCall>(table[kPoppedMoveSlot])(controller.bytes + 16, nullptr);
  Put(controller.bytes, 16, reinterpret_cast<uintptr_t>(At(kMoveStates[0].table)));
  Put(frame, 0x4C, 0);
  if (preserve_callbacks) Put(controller.bytes, 0x480, 0xBAD00000u);
}
DWORD WINAPI OtherThread(void*) {
  bool clear = true;
  assert(!NativeAiMoveDelegatesCleared(controller.bytes, &clear) && !clear);
  assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kRejected);
  assert(!PrepareNativeAiMovement(GetModuleHandleW(nullptr), g_movement.thread, controller.bytes));
  return 0;
}
void VerifyFile(const char* path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  assert(input && input.tellg() == 37667328);
  std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
  input.seekg(0); assert(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
  assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 4096);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
  assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == kMovementImageBase);
  for (const auto& state : kMoveStates) {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    const uint8_t* table = nullptr;
    for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n, ++section) {
      const auto rva = state.table - kMovementImageBase;
      if (rva >= section->VirtualAddress && rva - section->VirtualAddress + 0x2A0 <= section->SizeOfRawData)
        table = bytes.data() + section->PointerToRawData + rva - section->VirtualAddress;
    }
    assert(table && Read(table, 0x29C) == state.cancel && Read(table, 0x44) == state.popped);
    if (state.moving) {
      assert(!std::memcmp(bytes.data() + state.cancel - 0x10900C00, At(state.cancel), sizeof(kCancelPositionCode)));
      assert(!std::memcmp(bytes.data() + state.popped - 0x10900C00, At(state.popped), sizeof(kPoppedPositionCode)));
    }
  }
  assert(bytes[0x109D4530 - 0x10900C00] == 0xC3);
  assert(!std::memcmp(bytes.data() + 0x11700480 - 0x10900C00, kPopStateHead, sizeof(kPopStateHead)));
  std::puts("Native AI movement: 16 state slots and 4 full method bodies match TWFC.exe");
}
}  // namespace

namespace wfcds {
bool UnrealObjectValid(void* p) {
  return (p == controller.bytes && controller.valid) || (p == world.bytes && world.valid) ||
      (p == state_node.bytes && state_node.valid);
}
bool UnrealObjectIsA(void* p, const wchar_t* type) {
  return UnrealObjectValid(p) && ((p == controller.bytes && !std::wcscmp(type, L"TnAiController")) ||
      (p == world.bytes && !std::wcscmp(type, L"WorldInfo")));
}
bool UnrealReadBoolProperty(void* p, const wchar_t* name, bool* out) {
  if (!UnrealObjectValid(p) || std::wcscmp(name, L"bDeleteMe")) return false;
  *out = deleting; return true;
}
bool UnrealReadByteProperty(void* p, const wchar_t* name, uint8_t* out) {
  if (p != world.bytes || !world.valid || std::wcscmp(name, L"NetMode")) return false;
  *out = net_mode; return true;
}
bool UnrealReadObjectProperty(void* p, const wchar_t* name, void** out) {
  if (p != controller.bytes || !controller.valid) return false;
  if (!std::wcscmp(name, L"WorldInfo")) *out = reinterpret_cast<void*>(Read(p, 0x98));
  else if (!std::wcscmp(name, L"PathFocus")) *out = reinterpret_cast<void*>(Read(p, 0x31C));
  else return false;
  return !*out || UnrealObjectValid(*out);
}
bool UnrealResolveObjectPropertyOffset(void*, const wchar_t* name, uint32_t* out) {
  if (!std::wcscmp(name, L"WorldInfo")) *out = 0x98;
  else if (!std::wcscmp(name, L"PathFocus")) *out = 0x31C;
  else return false;
  return true;
}
bool UnrealReadDelegateProperty(void* p, const wchar_t* name, UnrealScriptDelegate* out, uint32_t* offset) {
  if (p != controller.bytes || !controller.valid || delegates_missing) return false;
  uint32_t field = 0;
  if (!std::wcscmp(name, L"__NearDestinationNotify__Delegate")) field = 0x464;
  else if (!std::wcscmp(name, L"__MoveFailedNotify__Delegate")) field = 0x474;
  else return false;
  std::memcpy(out, controller.bytes + field, sizeof(*out));
  if (offset) *offset = field + (wrong_offset ? 4 : 0);
  return true;
}
}  // namespace wfcds

int main(int argc, char** argv) {
  test_image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x02300000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(test_image);
  const auto module = reinterpret_cast<HMODULE>(test_image);
  *At(0x109D4530) = 0xC3;
  std::memcpy(At(0x11700480), kPopStateHead, sizeof(kPopStateHead));
  for (int i = 1; i <= 2; ++i) {
    const auto& state = kMoveStates[i];
    std::memcpy(At(state.cancel), kCancelPositionCode, sizeof(kCancelPositionCode));
    std::memcpy(At(state.popped), kPoppedPositionCode, sizeof(kPoppedPositionCode));
    if (i == 2) {
      At(state.cancel)[32] = 0x7C; At(state.cancel)[33] = 0x5A;
      At(state.popped)[40] = 0x24; At(state.popped)[41] = 0x1D;
    }
  }
  for (const auto& state : kMoveStates) {
    Put(At(state.table), 0x29C, reinterpret_cast<uintptr_t>(At(state.cancel)));
    Put(At(state.table), 0x44, reinterpret_cast<uintptr_t>(At(state.popped)));
  }
  if (argc == 2) VerifyFile(argv[1]);
  assert(BindMovementImage(module, GetCurrentThreadId(), &g_movement));
  MoveBindings rejected;
  for (const auto& state : kMoveStates) {
    for (auto slot : {0x29Cu, 0x44u}) {
      const auto expected = Read(At(state.table), slot);
      Put(At(state.table), slot, 0);
      assert(!BindMovementImage(module, GetCurrentThreadId(), &rejected));
      Put(At(state.table), slot, expected);
    }
    if (state.moving) {
      for (auto method : {state.cancel, state.popped}) {
        At(method)[10] ^= 1;
        assert(!BindMovementImage(module, GetCurrentThreadId(), &rejected));
        At(method)[10] ^= 1;
      }
    }
  }
  g_movement.module = GetModuleHandleW(nullptr); // Public prepare checks actual host identity.
  Put(controller.bytes, 0x98, reinterpret_cast<uintptr_t>(world.bytes));
  Put(controller.bytes, 16, reinterpret_cast<uintptr_t>(At(kMoveStates[0].table)));
  Put(controller.bytes, 0x28, reinterpret_cast<uintptr_t>(frame));
  Put(frame, 0x38, reinterpret_cast<uintptr_t>(state_node.bytes));
  Put(frame, 0x48, reinterpret_cast<uintptr_t>(state_stack));
  Put(frame, 0x50, 1);
  assert(PrepareNativeAiMovement(g_movement.module, g_movement.thread, controller.bytes));
  // Native CancelMove passes UObject in ESI and one false argument to PopState.
  uint8_t* out = At(0x11700480);
  const uint8_t pushes[] = {0xFF, 0x74, 0x24, 0x04, 0x56, 0xB8};
  std::memcpy(out, pushes, sizeof(pushes)); out += sizeof(pushes);
  Put(out, 0, reinterpret_cast<uintptr_t>(&PopFixture)); out += 4;
  const uint8_t tail[] = {0xFF, 0xD0, 0xC2, 0x04, 0x00};
  std::memcpy(out, tail, sizeof(tail));
  *At(0x116FC820) = 0xC3; // GC fixture preserves EDX, as shipping callback does.
  DWORD old = 0;
  assert(VirtualProtect(test_image, 0x01E00000, PAGE_EXECUTE_READ, &old));
  FlushInstructionCache(GetCurrentProcess(), test_image, 0x01E00000);
  auto moving = [&](int state) {
    Put(controller.bytes, 16, reinterpret_cast<uintptr_t>(At(kMoveStates[state].table)));
    Put(controller.bytes, 0x31C, reinterpret_cast<uintptr_t>(state_node.bytes));
    std::memset(controller.bytes + 0x464, 0xAB, 32);
    Put(frame, 0x4C, 1);
  };
  bool cleared = false;
  pop_reentry = true;
  for (unsigned n = 0; n < 3000; ++n) {
    for (int state = 1; state <= 2; ++state) {
      moving(state);
      assert(NativeAiMoveDelegatesCleared(controller.bytes, &cleared) && !cleared);
      assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kApplied);
      assert(Read(controller.bytes, 0x31C) == 0 && Read(frame, 0x4C) == 0);
      assert(NativeAiMoveDelegatesCleared(controller.bytes, &cleared) && cleared);
      const auto calls = pops;
      assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kApplied);
      assert(pops == calls); // Nonmoving base no-op, never pops an unrelated state.
    }
  }
  assert(pops == 6000);
  for (const auto& state : kMoveStates) {
    if (state.moving) continue;
    Put(controller.bytes, 16, reinterpret_cast<uintptr_t>(At(state.table)));
    assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kApplied);
  }
  for (size_t field = 0x464; field < 0x484; field += 4) {
    Put(controller.bytes, field, 0xBAD00000u);
    assert(NativeAiMoveDelegatesCleared(controller.bytes, &cleared) && !cleared);
    assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kStateMismatch);
    Put(controller.bytes, field, 0);
  }
  auto no_mutation = [&]() {
    const auto calls = pops;
    assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kRejected);
    assert(pops == calls);
  };
  moving(1);
  delegates_missing = true; no_mutation(); delegates_missing = false;
  wrong_offset = true; no_mutation(); wrong_offset = false;
  deleting = true; no_mutation(); deleting = false;
  net_mode = 1; no_mutation(); net_mode = 0;
  controller.valid = false; no_mutation(); controller.valid = true;
  state_node.valid = false; no_mutation(); state_node.valid = true;
  Put(frame, 0x4C, 0); no_mutation(); Put(frame, 0x4C, 1);
  Put(frame, 0x48, 0xBAD00000u); no_mutation(); Put(frame, 0x48, reinterpret_cast<uintptr_t>(state_stack));
  Put(controller.bytes, 0x28, 0xBAD00000u); no_mutation(); Put(controller.bytes, 0x28, reinterpret_cast<uintptr_t>(frame));
  Put(At(kMoveStates[1].table), 0x44, 0xBAD00000u); no_mutation();
  Put(At(kMoveStates[1].table), 0x44, reinterpret_cast<uintptr_t>(At(kMoveStates[1].popped)));
  ignore_pop = true;
  assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kStateMismatch);
  ignore_pop = false;
  preserve_callbacks = true;
  assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kStateMismatch);
  preserve_callbacks = false;
  moving(2);
  world.bytes[0x39C] = 1;
  const auto calls = pops;
  assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kStateMismatch);
  assert(pops == calls); // Native authority guard still executes.
  world.bytes[0x39C] = 0;
  HANDLE thread = CreateThread(nullptr, 0, &OtherThread, nullptr, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  assert(NativeAiCancelMove(controller.bytes) == NativeAiMovementResult::kApplied);
  g_movement = {};
  assert(VirtualFree(test_image, 0, MEM_RELEASE));
  std::puts("NativeAiMovementTests: 6000 exact-body movement cancellations passed");
}
