// Exercise the actual resolver, packed identities and assembly call adapter
// against a small in-process object graph. No game process or APK is needed.
#include "UnrealBridge.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>

using namespace wfcds;

namespace {
struct alignas(16) Object { uint8_t bytes[0x180] = {}; };
struct NameEntry { uint32_t index = 0; uint32_t next = 0; wchar_t text[64] = {}; };
std::array<Object, 256> objects;
std::array<void*, 256> object_slots;
std::array<NameEntry, 128> names;
std::array<void*, 128> name_slots;
std::array<void*, 4096> classes;
std::array<void*, 64> vtable;
UnrealArray object_array;
UnrealArray name_array;
void* engine_result;
uint32_t engine_name;
size_t name_count;
size_t object_count;
int live_allocations = 0;
int fail_resize_after = -1;
int selection_calls = 0;
unsigned collision_calls=0;
uint32_t collision_mask=0;
void* collision_receiver=nullptr;
void __attribute__((stdcall)) InvokeTestCollision(void* object,void* parameters) {
  assert(object==collision_receiver);
  const auto* bytes=static_cast<uint8_t*>(parameters);
  assert(bytes[0]==15 && !bytes[1] && !bytes[2] && !bytes[3]);
  uint32_t value=0;std::memcpy(&value,bytes+4,4);assert(value==collision_mask);++collision_calls;
}
UnrealFunctionExecDetour context_detour;
void* expected_context = nullptr;
void* expected_frame = nullptr;
void* expected_edx = nullptr;
int original_calls = 0;
int replacement_calls = 0;
UnrealScriptDelegate expected_delegate;
int delegate_calls = 0;
unsigned barrier_calls = 0;
void** barrier_slot = nullptr;
void __attribute__((cdecl)) TestReferenceBarrier(void* value) {
  assert(barrier_slot && *barrier_slot == value); // AFTER successful publication.
  ++barrier_calls;
}
DWORD WINAPI WrongReferenceThread(void* object) {
  assert(!UnrealObjectReferenceWritesReady());
  assert(!UnrealWriteObjectPropertyExpected(object, L"OwnerRef", nullptr, object));
  return 0;
}
DWORD WINAPI WrongBoolThread(void* object) {
  assert(!UnrealWriteBoolPropertyExpected(object, L"_bIconicMode", false, true));
  return 0;
}
void __attribute__((stdcall)) InvokeTestDelegate(void* object, void* parameters) {
  assert(UnrealObjectValid(object));
  assert(*static_cast<int32_t*>(parameters) == 9);
  assert(std::memcmp(static_cast<uint8_t*>(parameters) + 4,
                     &expected_delegate, sizeof(expected_delegate)) == 0);
  ++delegate_calls;
}
bool expect_compiled_context = false;
void WFCDS_UNREAL_EXEC_CALL OriginalContext(void* context, void* edx, void* frame, void* result) {
  assert(context == static_cast<uint8_t*>(expected_context) + (expect_compiled_context ? 16 : 0));
  assert(edx == expected_edx && frame == expected_frame);
  ++original_calls;
  *static_cast<int*>(result) = 123;
}
void WFCDS_UNREAL_EXEC_CALL ReplacementContext(void* context, void* edx, void* frame, void* result) {
  assert(context == expected_context && UnrealObjectValid(context));
  ++replacement_calls;
  context_detour.original_executor(context, edx, frame, result);
}
void __attribute__((stdcall)) InvokeTestSelection(void* object, void* parameters) {
  assert(UnrealObjectValid(object));
  assert(*static_cast<int32_t*>(parameters) == 17);
  const auto* name = reinterpret_cast<const UnrealArrayView*>(
      static_cast<const uint8_t*>(parameters) + 4);
  assert(std::wcscmp(static_cast<const wchar_t*>(name->data), L"MissionShell") == 0);
  ++selection_calls;
}
void WFCDS_UNREAL_EXEC_CALL ResizeTestArray(void* header, void*, int32_t,
                                            int32_t count, int32_t bytes) {
  auto& array = *static_cast<UnrealArrayView*>(header);
  assert(!array.data || count == 0);  // Staging never reallocates live data.
  if (count) {
    if (fail_resize_after == 0) return;
    if (fail_resize_after > 0) --fail_resize_after;
    array.data = HeapAlloc(GetProcessHeap(), 0, count * bytes);
    assert(array.data);
    ++live_allocations;
  } else if (array.data) {
    assert(HeapFree(GetProcessHeap(), 0, array.data));
    array.data = nullptr;
    --live_allocations;
  }
}
void* WFCDS_UNREAL_EXEC_CALL AssignTestString(void* destination, void*, const void* source) {
  auto& dst = *static_cast<UnrealArrayView*>(destination);
  const auto& src = *static_cast<const UnrealArrayView*>(source);
  if (dst.data) { assert(HeapFree(GetProcessHeap(), 0, dst.data)); --live_allocations; }
  dst = {};
  if (src.count) {
    dst.data = HeapAlloc(GetProcessHeap(), 0, src.count * sizeof(wchar_t));
    assert(dst.data);
    ++live_allocations;
    dst.count = dst.capacity = src.count;
    std::memcpy(dst.data, src.data, src.count * sizeof(wchar_t));
  }
  return destination;
}

template<typename T> void Put(void* object, size_t offset, T value) {
  std::memcpy(static_cast<uint8_t*>(object) + offset, &value, sizeof(value));
}
uint32_t Name(const wchar_t* text) {
  for (size_t i = 0; i < name_count; ++i)
    if (std::wcscmp(names[i].text, text) == 0) return static_cast<uint32_t>(i << 14);
  assert(name_count<names.size());
  const size_t i = name_count++;
  names[i].index = static_cast<uint32_t>(i);
  std::wcscpy(names[i].text, text);
  name_slots[i] = &names[i];
  name_array.count = static_cast<int32_t>(name_count);
  return static_cast<uint32_t>(i << 14);
}
void __attribute__((stdcall)) MakeTestName(uint32_t* out, const wchar_t* text,
                                         uint32_t, uint32_t, uint32_t) {
  *out = 0;
  for (size_t i = 1; i < name_count; ++i)
    if (std::wcscmp(names[i].text, text) == 0) *out = static_cast<uint32_t>(i << 14);
}
void* WFCDS_UNREAL_EXEC_CALL FindTestFunction(void*, void*, uint32_t name, uint32_t reserved) {
  assert(reserved == 0);
  engine_name = name;
  return engine_result;
}
void* MakeObject(const wchar_t* name, uint32_t cls, void* outer = nullptr) {
  assert(object_count<objects.size());
  void* result = &objects[object_count];
  object_slots[object_count++] = result;
  object_array.count = static_cast<int32_t>(object_count);
  Put(result, 0, vtable.data());
  Put(result, 4, Name(name));
  Put(result, 0x14, cls | ((outer ? UnrealObjectIndex(outer) : 0) << 12));
  Put(result, 0x18, static_cast<uint32_t>(object_count));
  return result;
}
void Link(void* owner, void* first) { Put(owner, 0x30, first); }
void Script(void* function) {
  Put(function, 0x54, uint32_t{0x02020002});
  Put(function, 0x60, uint16_t{0xFFFF});
  Put(function, 0x68, &SuppressedIdentitySideEffect);
}
}

#include "UnrealObjectCloneTests.inc"

int main(int argc,char** argv) {
  // Crossing a readable VirtualQuery boundary is legal; crossing a guard or
  // inaccessible page is not. This reproduces the production memory gate.
  SYSTEM_INFO system = {};
  GetSystemInfo(&system);
  const size_t page = system.dwPageSize;
  auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(memory);
  DWORD old = 0;
  assert(VirtualProtect(memory + page, page, PAGE_READONLY, &old));
  assert(Readable(memory + page - 16, 32));
  ClearWorldReflectionCaches();
  assert(VirtualProtect(memory + page, page, PAGE_NOACCESS, &old));
  assert(!Readable(memory + page - 16, 32));
  ClearWorldReflectionCaches();
  assert(VirtualProtect(memory + page, page, PAGE_READWRITE | PAGE_GUARD, &old));
  assert(!Readable(memory + page - 16, 32));
  ClearWorldReflectionCaches();
  assert(VirtualFree(memory, 0, MEM_RELEASE));
  object_array = {object_slots.data(), 0, static_cast<int32_t>(object_slots.size())};
  name_array = {name_slots.data(), 0, static_cast<int32_t>(name_slots.size())};
  g_objects = &object_array;
  g_names = &name_array;
  g_class_table = classes.data();
  g_make_name = &MakeTestName;
  g_ready = true;
  Name(L"None");
  vtable[kFindFunctionVtableOffset / 4] = reinterpret_cast<void*>(&FindTestFunction);
  classes[1] = MakeObject(L"Class", 1);
  classes[2] = MakeObject(L"State", 1);
  classes[3] = MakeObject(L"Struct", 1);
  classes[4] = MakeObject(L"Function", 1);
  classes[5] = MakeObject(L"Object", 1);
  classes[6] = MakeObject(L"Package", 1);
  Put(classes[1], 0x28, classes[2]);
  Put(classes[2], 0x28, classes[3]);
  Put(classes[3], 0x28, classes[5]);
  Put(classes[4], 0x28, classes[3]);
  void* package = MakeObject(L"TransGame", 6);
  classes[7] = MakeObject(L"TnGame", 1, package);
  classes[8] = MakeObject(L"OtherGame", 1, package);
  void* function = MakeObject(L"StartLevel", 4, classes[7]);
  Script(function);
  Link(classes[7], function);
  void* wrong_owner = MakeObject(L"StartLevel", 4, classes[8]);
  Script(wrong_owner);
  Link(classes[8], wrong_owner);
  void* state = MakeObject(L"Walking", 2, classes[7]);
  Put(function, 0x2C, state);
  void* state_function = MakeObject(L"StartLevel", 4, state);
  Script(state_function);
  Link(state, state_function);
  void* receiver = MakeObject(L"Instance", 7);
  UnrealFunctionHandle found;
  assert(UnrealBridgeReady());
  assert(UnrealObjectOuter(function)==classes[7]);
  assert(UnrealObjectOuter(state_function)==state);
  assert(UnrealObjectOuter(classes[7])==package);
  assert(!UnrealObjectOuter(receiver) && !UnrealObjectOuter(nullptr));
  uint32_t outer_identity=0;std::memcpy(&outer_identity,static_cast<uint8_t*>(function)+0x14,4);
  Put(function,0x14,(outer_identity&0xFFFu)|(UnrealObjectIndex(function)<<12));
  assert(!UnrealObjectOuter(function));
  Put(function,0x14,(outer_identity&0xFFFu)|(0x3FFFFu<<12));
  assert(!UnrealObjectOuter(function));
  Put(function,0x14,outer_identity);
  const auto outer_slot=UnrealObjectIndex(classes[7])-1;
  void* old_outer_slot=object_slots[outer_slot];object_slots[outer_slot]=receiver;
  assert(!UnrealObjectOuter(function));object_slots[outer_slot]=old_outer_slot;
  g_ready=false;assert(!UnrealObjectOuter(function));g_ready=true;
  const auto heat_name=Name(L"WeaponOverheated"),heat_alias=Name(L"CookedHeatNameAlias");
  std::wcscpy(names[heat_alias>>14].text,L"WeaponOverheated");
  assert(UnrealUnnumberedNameEquals(heat_name,L"WeaponOverheated"));
  assert(UnrealUnnumberedNameEquals(heat_alias,L"weaponoverheated"));
  assert(!UnrealUnnumberedNameEquals(heat_name|1,L"WeaponOverheated"));
  assert(!UnrealUnnumberedNameEquals(heat_name>>14,L"WeaponOverheated"));
  assert(!UnrealUnnumberedNameEquals(0xFFFFC000,L"WeaponOverheated"));
  assert(!UnrealUnnumberedNameEquals(heat_name,L"WeaponCooled"));
  assert(!UnrealUnnumberedNameEquals(heat_name,nullptr));
  assert(!UnrealUnnumberedNameEquals(heat_name,L""));
  g_ready=false;assert(!UnrealUnnumberedNameEquals(heat_name,L"WeaponOverheated"));g_ready=true;
  assert(UnrealObjectValid(function));
  assert(UnrealObjectIsA(function, L"Function"));
  assert(ValidScriptFunction(function));
  assert(OwnerPathMatches(classes[7], L"TransGame.TnGame"));
  assert(Readable(g_class_table, 4096 * sizeof(void*)));
  uint32_t test_name = 0;
  assert(MakeName(L"TnGame", &test_name));
  assert(BaseNameIndexEquals(classes[7], test_name >> 14));
  assert(UnrealObjectIsA(classes[7], L"Class"));
  assert(FindMetadataOwner(L"TransGame.TnGame") == classes[7]);
  assert(BuildExactFunctionHandle(function, &found));
  assert(UnrealResolveExactScriptFunction(0, L"TransGame.TnGame", L"StartLevel", &found));
  assert(found.function == function && found.owner == classes[7]);
  assert(UnrealResolveExactScriptFunction(0, L"TransGame.TnGame.Walking", L"StartLevel", &found));
  assert(found.function == state_function && found.context_class == classes[7]);
  assert(!UnrealResolveExactScriptFunction(UnrealObjectIndex(wrong_owner),
      L"TransGame.TnGame", L"StartLevel", &found));
  assert(!UnrealResolveExactScriptFunction(0, L"TransGame.Absent", L"StartLevel", &found));
  engine_result = function;
  assert(FindFunction(receiver, L"StartLevel") == function);
  assert(engine_name == Name(L"StartLevel"));
  engine_result = nullptr;
  assert(FindFunction(receiver, L"StartLevel") == nullptr);
  // Metadata lookup does not depend on an instance, active state, or its virtual lookup.
  assert(UnrealResolveExactScriptFunctionForObject(receiver, L"TransGame.TnGame", L"StartLevel", &found));
  // Names from parsed owner paths must not retain pointers into temporary strings.
  Name(L"TemporaryName");
  uint32_t packed = 0;
  {
    std::wstring temporary = L"TemporaryName";
    assert(MakeName(temporary.c_str(), &packed));
    temporary.assign(L"OverwriteName");
  }
  assert(MakeName(L"TemporaryName", &packed) && packed == Name(L"TemporaryName"));
  // A Defined UFunction with a stripped/null executor is not a successful
  // no-op. Empty declarations remain legal; defined methods must execute.
  Put(function, 0x68, static_cast<void*>(nullptr));
  assert(!ValidCallableFunction(function));
  engine_result = function;
  assert(!UnrealCanCallVoid(receiver, L"StartLevel"));
  Put(function, 0x54, uint32_t{0x02020000});
  assert(ValidCallableFunction(function));
  Script(function);
  assert(BuildExactFunctionHandle(function, &found));
  // Script+0x40 is an unrelated property-array pointer, NOT capacity.
  uint8_t bytecode[8] = {0x2E};
  std::memcpy(bytecode + 1, &classes[7], sizeof(void*));
  Put(function, 0x38, bytecode);
  Put(function, 0x3C, int32_t{8});
  Put(function, 0x40, uintptr_t{0xF1234000});
  assert(UnrealValidateScriptObjectOperand(found, 0, 0x2E, classes[7]));
  Put(function, 0x3C, int32_t{4});
  assert(!UnrealValidateScriptObjectOperand(found, 0, 0x2E, classes[7]));
  Put(function, 0x3C, int32_t{8});
  Put(function, 0x54, uint32_t{0x02020022});
  assert(!UnrealValidateScriptObjectOperand(found, 0, 0x2E, classes[7]));
  // Execute actual x86 thunks both ways: compiled script enters with +0x10,
  // our callbacks receive UObject*, and forwarding restores the native ABI.
  expected_context = receiver;
  int frame_storage = 42;
  expected_frame = &frame_storage;
  expected_edx = &engine_name;
  for (bool compiled : {true, false, true}) {
    expect_compiled_context = compiled;
    Put(function, 0x54, uint32_t{0x02020002u | (compiled ? kFunctionNativeScript : 0u)});
    Put(function, 0x68, &OriginalContext);
    assert(UnrealInstallFunctionExecDetour(found, &ReplacementContext, &context_detour));
    assert(!UnrealInstallFunctionExecDetour(found, &ReplacementContext, &context_detour));
    auto entry = *reinterpret_cast<UnrealFunctionExec*>(static_cast<uint8_t*>(function) + 0x68);
    int result = 0;
    for (int tap = 0; tap < 1000; ++tap) {
      entry(static_cast<uint8_t*>(receiver) + (compiled ? 16 : 0),
            expected_edx, expected_frame, &result);
      assert(result == 123);
    }
    assert(UnrealRestoreFunctionExecDetour(&context_detour));
    assert(*reinterpret_cast<UnrealFunctionExec*>(static_cast<uint8_t*>(function) + 0x68) == &OriginalContext);
  }
  assert(original_calls == 3000 && replacement_calls == 3000);
  assert(g_function_exec_detour_count == 0);
  // Cached identities cannot survive a deleted/reused owner slot.
  object_slots[UnrealObjectIndex(classes[7]) - 1] = nullptr;
  assert(!FindMetadataOwner(L"TransGame.TnGame"));
  // Real production owned-array transaction, with isolated native allocators.
  classes[9] = MakeObject(L"ArrayProperty", 1);
  classes[10] = MakeObject(L"StrProperty", 1);
  void* definition = MakeObject(L"CharacterData", 3);
  void* weapons = MakeObject(L"WeaponTypes", 9, definition);
  void* abilities = MakeObject(L"Abilities", 9, definition);
  void* inner = MakeObject(L"Element", 10);
  Put(inner, 0x30, uint32_t{1 | (12 << 10)});
  Put(weapons, 0x30, uint32_t{1 | (12 << 10)});
  Put(abilities, 0x30, uint32_t{1 | (12 << 10) | (12 << 20)});
  Put(weapons, 0x40, inner);
  Put(abilities, 0x40, inner);
  Link(definition, weapons);
  Put(weapons, 0x2C, abilities);
  struct Data { UnrealArrayView weapons = {}; UnrealArrayView abilities = {}; } data;
  UnrealStructValueView value = {&data, sizeof(data), definition};
  g_fstring_assign = reinterpret_cast<void*>(&AssignTestString);
  g_array_resize_allocation = &ResizeTestArray;
  const wchar_t* original[] = {L"GunA", L"GunB", L"GrenadeBag"};
  UnrealStringArrayOverride seed{L"WeaponTypes", original, 3, false};
  assert(UnrealApplyOwnedStringArrayOverrides(value, &seed, 1));
  const wchar_t* guns[] = {L"NewGun", L"OtherGun"};
  const wchar_t* powers[] = {L"Dash", L"Barrier"};
  UnrealStringArrayOverride edits[] = {
      {L"WeaponTypes", guns, 2, true}, {L"Abilities", powers, 2, false}};
  const auto preserved_data = data.weapons.data;
  const int baseline = live_allocations;
  fail_resize_after = 1;  // Fail second field, after staging the first.
  assert(!UnrealApplyOwnedStringArrayOverrides(value, edits, 2));
  assert(data.weapons.data == preserved_data && data.abilities.count == 0);
  assert(live_allocations == baseline);
  fail_resize_after = -1;
  assert(UnrealApplyOwnedStringArrayOverrides(value, edits, 2));
  assert(data.weapons.count == 3 && data.abilities.count == 2);
  UnrealArrayView typed = {};
  assert(UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed));
  assert(typed.data == data.weapons.data && typed.count == 3);
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"ClassProperty", 4, &typed));
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 4, &typed));
  const int32_t saved_capacity = data.weapons.capacity;
  data.weapons.capacity = 1;
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed));
  data.weapons.capacity = saved_capacity;
  data.weapons.capacity = 128;
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed));
  assert(UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed, 256));
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed, 0));
  assert(!UnrealReadStructTypedArray(value, L"WeaponTypes", L"StrProperty", 12, &typed, 65537));
  data.weapons.capacity = saved_capacity;
  auto* weapon_strings = static_cast<UnrealArrayView*>(data.weapons.data);
  assert(std::wcscmp(static_cast<wchar_t*>(weapon_strings[0].data), L"NewGun") == 0);
  assert(std::wcscmp(static_cast<wchar_t*>(weapon_strings[2].data), L"GrenadeBag") == 0);
  // Invalid late field and duplicate destination cannot partially commit.
  edits[1].field_name = L"Missing";
  const auto committed_data = data.weapons.data;
  const int committed_allocations = live_allocations;
  assert(!UnrealApplyOwnedStringArrayOverrides(value, edits, 2));
  assert(data.weapons.data == committed_data && live_allocations == committed_allocations);
  edits[1].field_name = L"WeaponTypes";
  assert(!UnrealApplyOwnedStringArrayOverrides(value, edits, 2));
  assert(data.weapons.data == committed_data && live_allocations == committed_allocations);
  const wchar_t* sparse[] = {L"", L"Barrier"};
  UnrealStringArrayOverride sparse_edit{L"Abilities", sparse, 2, false};
  assert(UnrealApplyOwnedStringArrayOverrides(value, &sparse_edit, 1));
  assert(static_cast<wchar_t*>(static_cast<UnrealArrayView*>(data.abilities.data)[0].data)[0] == 0);
  UnrealStringArrayOverride clear[] = {
      {L"WeaponTypes", nullptr, 0, false}, {L"Abilities", nullptr, 0, false}};
  assert(UnrealApplyOwnedStringArrayOverrides(value, clear, 2));
  assert(!data.weapons.data && !data.abilities.data && live_allocations == 0);
  // SelectCharacter has TWO inputs; Ignored is a real int parameter, not a
  // decompiler local. Verify exact named-field marshalling through Invoke.S.
  classes[11] = MakeObject(L"IntProperty", 1);
  void* binding = MakeObject(L"Binding", 8);
  void* select = MakeObject(L"SelectCharacter", 4, classes[8]);
  Script(select);
  Put(select, 0x5D, uint8_t{2});
  Put(select, 0x5E, uint16_t{16});
  void* ignored = MakeObject(L"Ignored", 11, select);
  void* character = MakeObject(L"CharacterName", 10, select);
  Put(ignored, 0x30, uint32_t{1 | (4 << 10)});
  Put(character, 0x30, uint32_t{1 | (12 << 10) | (4 << 20)});
  Put(ignored, 0x38, uint32_t{kPropertyParm});
  Put(character, 0x38, uint32_t{kPropertyParm});
  Link(select, ignored);
  Put(ignored, 0x2C, character);
  UnrealFunctionHandle select_handle;
  assert(BuildExactFunctionHandle(select, &select_handle));
  g_invoke = reinterpret_cast<void*>(&InvokeTestSelection);
  assert(UnrealCallExactVoidStringInt(binding, select_handle,
      L"CharacterName", L"MissionShell", L"Ignored", 17));
  assert(selection_calls == 1);
  assert(!UnrealCallExactVoidStringInt(binding, select_handle,
      L"CharacterName", L"MissionShell", L"Missing", 17));
  Put(select, 0x5D, uint8_t{1});
  assert(!UnrealCallExactVoidStringInt(binding, select_handle,
      L"CharacterName", L"MissionShell", L"Ignored", 17));
  assert(selection_calls == 1);
  // WFC FScriptDelegate is 16 bytes, not UE3's eight-byte UObject/FName pair.
  // Keep the native cached context AND function through capture and replay.
  classes[12] = MakeObject(L"DelegateProperty", 1);
  void* save = MakeObject(L"SaveActiveCheckpoint", 4, classes[8]);
  Script(save);
  Put(save, 0x5D, uint8_t{2});
  Put(save, 0x5E, uint16_t{20});
  void* index = MakeObject(L"CheckpointIndex", 11, save);
  void* complete = MakeObject(L"OnComplete", 12, save);
  Put(index, 0x30, uint32_t{1 | (4 << 10)});
  Put(index, 0x38, uint32_t{kPropertyParm});
  Put(complete, 0x30, uint32_t{1 | (16 << 10) | (4 << 20)});
  Put(complete, 0x38, uint32_t{kPropertyParm});
  Link(save, index);
  Put(index, 0x2C, complete);
  UnrealFunctionHandle save_handle;
  assert(BuildExactFunctionHandle(save, &save_handle));
  assert(UnrealValidateExactIntDelegateCall(save_handle, L"CheckpointIndex", L"OnComplete"));
  expected_delegate = {binding, Name(L"Complete"), static_cast<uint8_t*>(binding) + 16,
                       reinterpret_cast<void*>(&OriginalContext)};
  uint8_t locals[20] = {};
  uint8_t frame[0x20] = {};
  std::memcpy(locals + 4, &expected_delegate, sizeof(expected_delegate));
  Put(frame, 0x10, locals);
  UnrealScriptDelegate captured;
  assert(UnrealReadFrameDelegateParameter(frame, 4, &captured));
  assert(std::memcmp(&captured, &expected_delegate, sizeof(captured)) == 0);
  assert(!UnrealReadFrameDelegateParameter(frame, 1010, &captured));
  g_invoke = reinterpret_cast<void*>(&InvokeTestDelegate);
  assert(UnrealCallExactVoidIntDelegate(binding, save_handle, L"CheckpointIndex", 9,
                                       L"OnComplete", captured));
  assert(delegate_calls == 1);
  captured.compiled_function = nullptr;
  assert(!UnrealCallExactVoidIntDelegate(binding, save_handle, L"CheckpointIndex", 9,
                                        L"OnComplete", captured));
  Put(complete, 0x30, uint32_t{1 | (8 << 10) | (4 << 20)});
  assert(!UnrealValidateExactIntDelegateCall(save_handle, L"CheckpointIndex", L"OnComplete"));
  assert(delegate_calls == 1);
  // The same four-word layout is retained on object properties. A stale
  // cached compiled callback is not lost just because UObject/FName are None.
  Link(classes[8], complete);
  Put(complete, 0x30, uint32_t{1 | (16 << 10) | (0x100 << 20)});
  expected_delegate.object = nullptr;
  expected_delegate.function_name = 0;
  std::memcpy(static_cast<uint8_t*>(binding) + 0x100, &expected_delegate, 16);
  uint32_t delegate_offset = 0;
  assert(UnrealReadDelegateProperty(binding, L"OnComplete", &captured, &delegate_offset));
  assert(delegate_offset == 0x100 && std::memcmp(&captured, &expected_delegate, 16) == 0);
  Put(complete, 0x30, uint32_t{1 | (8 << 10) | (0x100 << 20)});
  assert(!UnrealReadDelegateProperty(binding, L"OnComplete", &captured));
  Put(complete, 0x30, uint32_t{2 | (16 << 10) | (0x100 << 20)});
  assert(!UnrealReadDelegateProperty(binding, L"OnComplete", &captured));
  assert(!UnrealReadDelegateProperty(binding, L"AbsentDelegate", &captured));
  // Raw custom ownership writes must participate in WFC GC, only after a
  // successful expected-value comparison and only on the game thread.
  classes[13] = MakeObject(L"ObjectProperty", 1);
  void* reference = MakeObject(L"OwnerRef", 13, classes[8]);
  Link(classes[8], reference);
  Put(reference, 0x30, uint32_t{1 | (4 << 10) | (0x104 << 20)});
  barrier_slot = reinterpret_cast<void**>(static_cast<uint8_t*>(binding) + 0x104);
  *barrier_slot = nullptr;
  assert(!UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", nullptr, binding));
  g_object_reference_barrier = &TestReferenceBarrier;
  assert(!UnrealObjectReferenceWritesReady());
  UnrealBeginGameThreadPulse();
  assert(UnrealObjectReferenceWritesReady());
  assert(UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", nullptr, binding));
  assert(barrier_calls == 1 && *barrier_slot == binding);
  assert(!UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", nullptr, package));
  assert(barrier_calls == 1 && *barrier_slot == binding);
  assert(UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", binding, package));
  assert(barrier_calls == 2 && *barrier_slot == package);
  assert(UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", package, nullptr));
  assert(barrier_calls == 3 && !*barrier_slot);
  HANDLE wrong_thread = CreateThread(nullptr, 0, WrongReferenceThread, binding, 0, nullptr);
  assert(wrong_thread && WaitForSingleObject(wrong_thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(wrong_thread);
  assert(barrier_calls == 3 && !*barrier_slot);
  for (uint32_t layout : {uint32_t{2 | (4 << 10) | (0x104 << 20)},
                           uint32_t{1 | (8 << 10) | (0x104 << 20)},
                           uint32_t{1 | (4 << 10) | (0x105 << 20)}}) {
    Put(reference, 0x30, layout);
    assert(!UnrealWriteObjectPropertyExpected(binding, L"OwnerRef", nullptr, binding));
  }
  assert(barrier_calls == 3 && !*barrier_slot);
  // A readable UObject field is not necessarily writable. Reject a protected
  // page before attempting the atomic write (and before notifying GC).
  Put(reference, 0x30, uint32_t{1 | (4 << 10) | (0x104 << 20)});
  auto* protected_object = static_cast<uint8_t*>(VirtualAlloc(nullptr, page,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(protected_object);
  std::memcpy(protected_object, binding, sizeof(Object));
  const auto binding_index = UnrealObjectIndex(binding);
  object_slots[binding_index - 1] = protected_object;
  barrier_slot = reinterpret_cast<void**>(protected_object + 0x104);
  assert(VirtualProtect(protected_object, page, PAGE_READONLY, &old));
  assert(UnrealObjectValid(protected_object));
  assert(!UnrealWriteObjectPropertyExpected(protected_object, L"OwnerRef", nullptr, package));
  assert(barrier_calls == 3 && !*barrier_slot);
  assert(VirtualProtect(protected_object, page, PAGE_READWRITE, &old));
  assert(UnrealWriteObjectPropertyExpected(protected_object, L"OwnerRef", nullptr, package));
  assert(barrier_calls == 4 && *barrier_slot == package);
  object_slots[binding_index - 1] = binding;
  ClearWorldReflectionCaches();
  assert(VirtualFree(protected_object, 0, MEM_RELEASE));
  barrier_slot = nullptr;
  g_object_reference_barrier = nullptr;
  assert(!UnrealObjectReferenceWritesReady());
  // Input locks are BYTE counters, not packed BoolProperty masks. Verify the
  // native-binding offset through real packed property metadata as well.
  classes[14] = MakeObject(L"ByteProperty", 1);
  void* input_counter = MakeObject(L"InputCounter", 14, classes[8]);
  Link(classes[8], input_counter);
  Put(input_counter, 0x30, uint32_t{1 | (1 << 10) | (0x10B << 20)});
  uint32_t counter_offset = 0;
  assert(UnrealResolveBytePropertyOffset(binding, L"InputCounter", &counter_offset));
  assert(counter_offset == 0x10B); // A byte need not be DWORD-aligned.
  for (uint32_t layout : {uint32_t{2 | (1 << 10) | (0x10B << 20)},
                           uint32_t{1 | (4 << 10) | (0x10B << 20)},
                           uint32_t{1 | (1 << 10) | (0x18 << 20)}}) {
    Put(input_counter, 0x30, layout);
    assert(!UnrealResolveBytePropertyOffset(binding, L"InputCounter", &counter_offset));
    assert(counter_offset == 0);
  }
  assert(!UnrealResolveBytePropertyOffset(binding, L"OwnerRef", &counter_offset));
  assert(!UnrealResolveBytePropertyOffset(nullptr, L"InputCounter", &counter_offset));
  assert(!UnrealResolveBytePropertyOffset(binding, nullptr, &counter_offset));
  assert(!UnrealResolveBytePropertyOffset(binding, L"InputCounter", nullptr));
  // Enemy observers are an ArrayProperty of complete 16-byte delegates. A
  // pointer-array readability check would miss an inaccessible cached callback.
  void* observers_field = MakeObject(L"NewEnemyObservers", 9, classes[8]);
  void* observer_inner = MakeObject(L"Observer", 12, observers_field);
  Link(classes[8], observers_field);
  constexpr uint32_t observer_layout = 1 | (12 << 10) | (0x110 << 20);
  constexpr uint32_t delegate_layout = 1 | (16 << 10);
  Put(observers_field, 0x30, observer_layout);
  Put(observers_field, 0x40, observer_inner);
  Put(observer_inner, 0x30, delegate_layout);
  UnrealScriptDelegate delegate_entries[2] = {expected_delegate, expected_delegate};
  UnrealArrayView delegate_array{delegate_entries, 2, 2}, observed_array;
  Put(binding, 0x110, delegate_array);
  uint32_t observer_offset = 0;
  assert(UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array, &observer_offset));
  assert(observer_offset == 0x110 && observed_array.count == 2 && observed_array.data == delegate_entries);
  for (auto layout : {uint32_t{2 | (12 << 10) | (0x110 << 20)},
                      uint32_t{1 | (4 << 10) | (0x110 << 20)},
                      uint32_t{1 | (12 << 10) | (0x18 << 20)}}) {
    Put(observers_field, 0x30, layout);
    assert(!UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array, &observer_offset));
    assert(!observed_array.data && !observer_offset);
  }
  Put(observers_field, 0x30, observer_layout);
  for (auto layout : {uint32_t{1 | (8 << 10)}, uint32_t{2 | (16 << 10)}}) {
    Put(observer_inner, 0x30, layout);
    assert(!UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  }
  Put(observer_inner, 0x30, delegate_layout);
  Put(observers_field, 0x40, input_counter);
  assert(!UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  Put(observers_field, 0x40, observer_inner);
  for (auto header : {UnrealArrayView{nullptr, 1, 1}, UnrealArrayView{nullptr, 0, 1},
                       UnrealArrayView{delegate_entries, -1, 2}, UnrealArrayView{delegate_entries, 2, 1},
                       UnrealArrayView{delegate_entries, 1, 4097}}) {
    Put(binding, 0x110, header);
    assert(!UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  }
  auto* boundary = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(boundary && VirtualProtect(boundary + page, page, PAGE_NOACCESS, &old));
  Put(binding, 0x110, UnrealArrayView{boundary + page - 8, 1, 1});
  assert(UnrealReadArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  assert(!UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  assert(VirtualFree(boundary, 0, MEM_RELEASE));
  Put(binding, 0x110, UnrealArrayView{});
  assert(UnrealReadDelegateArrayProperty(binding, L"NewEnemyObservers", &observed_array));
  assert(!observed_array.data && !observed_array.count);
  // The class/loadout menu changes a single packed Boolean on live GRI. Its
  // CAS must preserve every other flag and reject invalid/protected metadata.
  classes[15] = MakeObject(L"BoolProperty", 1);
  void* iconic_field = MakeObject(L"_bIconicMode", 15, classes[8]);
  Link(classes[8], iconic_field);
  constexpr uint32_t iconic_layout = 1 | (4 << 10) | (0x120 << 20);
  Put(iconic_field, 0x30, iconic_layout);
  auto* flag_word = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(binding) + 0x120);
  assert(!g_object_reference_barrier); // Boolean publication does not require GC.
  for (unsigned bit = 0; bit < 32; ++bit) {
    const uint32_t mask = uint32_t{1} << bit;
    Put(iconic_field, 0x40, mask);
    for (uint32_t pattern : {uint32_t{0}, uint32_t{0xFFFFFFFFu}, uint32_t{0xA5A5A5A5u}}) {
      *flag_word = pattern & ~mask;
      assert(!UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", true, false));
      assert(*flag_word == (pattern & ~mask));
      assert(UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", false, true));
      assert(*flag_word == (pattern | mask));
      assert(UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", true, true));
      assert(*flag_word == (pattern | mask));
      assert(UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", true, false));
      assert(*flag_word == (pattern & ~mask));
    }
  }
  Put(iconic_field, 0x40, uint32_t{1});
  *flag_word = 0xCAFE0000u;
  for (auto layout : {uint32_t{2 | (4 << 10) | (0x120 << 20)},
                      uint32_t{1 | (1 << 10) | (0x120 << 20)},
                      uint32_t{1 | (8 << 10) | (0x120 << 20)},
                      uint32_t{1 | (4 << 10) | (0x121 << 20)},
                      uint32_t{1 | (4 << 10) | (0x18 << 20)}}) {
    Put(iconic_field, 0x30, layout);
    assert(!UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", false, true));
    assert(*flag_word == 0xCAFE0000u);
  }
  Put(iconic_field, 0x30, iconic_layout);
  for (uint32_t mask : {uint32_t{0}, uint32_t{3}, uint32_t{0x80000001u}}) {
    Put(iconic_field, 0x40, mask);
    assert(!UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", false, true));
  }
  Put(iconic_field, 0x40, uint32_t{1});
  g_reference_write_thread = 0;
  assert(!UnrealWriteBoolPropertyExpected(binding, L"_bIconicMode", false, true));
  UnrealBeginGameThreadPulse();
  wrong_thread = CreateThread(nullptr, 0, WrongBoolThread, binding, 0, nullptr);
  assert(wrong_thread && WaitForSingleObject(wrong_thread, 5000) == WAIT_OBJECT_0);
  CloseHandle(wrong_thread);
  assert(*flag_word == 0xCAFE0000u);
  assert(!UnrealWriteBoolPropertyExpected(binding, nullptr, false, true));
  assert(!UnrealWriteBoolPropertyExpected(nullptr, L"_bIconicMode", false, true));
  assert(!UnrealWriteBoolPropertyExpected(binding, L"MissingFlag", false, true));
  protected_object = static_cast<uint8_t*>(VirtualAlloc(nullptr, page,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(protected_object);
  std::memcpy(protected_object, binding, sizeof(Object));
  object_slots[binding_index - 1] = protected_object;
  ClearWorldReflectionCaches();
  // Populate Readable's cache before downgrading protection. The current
  // VirtualQuery guard must reject even a no-op CAS on this readable page.
  assert(Readable(protected_object, sizeof(Object)));
  assert(VirtualProtect(protected_object, page, PAGE_READONLY, &old));
  assert(!UnrealWriteBoolPropertyExpected(protected_object, L"_bIconicMode", false, false));
  assert(!UnrealWriteBoolPropertyExpected(protected_object, L"_bIconicMode", false, true));
  assert(*reinterpret_cast<uint32_t*>(protected_object + 0x120) == 0xCAFE0000u);
  assert(VirtualProtect(protected_object, page, PAGE_READWRITE, &old));
  assert(UnrealWriteBoolPropertyExpected(protected_object, L"_bIconicMode", false, true));
  assert(*reinterpret_cast<uint32_t*>(protected_object + 0x120) == 0xCAFE0001u);
  object_slots[binding_index - 1] = binding;
  ClearWorldReflectionCaches();
  assert(VirtualFree(protected_object, 0, MEM_RELEASE));
  // Native acknowledgment reads eye height from the character blueprint. Its
  // float offset must come from scalar FloatProperty metadata, not a guessed
  // layout or a generic property read accepting a byte/array field.
  classes[16] = MakeObject(L"FloatProperty", 1);
  void* eye_height = MakeObject(L"BaseEyeHeight",16,classes[8]);
  Link(classes[8],eye_height);
  constexpr uint32_t eye_layout = 1 | (4 << 10) | (0x124 << 20);
  Put(eye_height,0x30,eye_layout);
  uint32_t eye_offset = 0;
  assert(UnrealResolveFloatPropertyOffset(binding,L"BaseEyeHeight",&eye_offset) && eye_offset == 0x124);
  for (uint32_t layout : {uint32_t{2 | (4 << 10) | (0x124 << 20)},
                           uint32_t{1 | (8 << 10) | (0x124 << 20)},
                           uint32_t{1 | (4 << 10) | (0x125 << 20)},
                           uint32_t{1 | (4 << 10) | (0x18 << 20)}}) {
    Put(eye_height,0x30,layout);
    assert(!UnrealResolveFloatPropertyOffset(binding,L"BaseEyeHeight",&eye_offset) && eye_offset == 0);
  }
  Put(eye_height,0x30,eye_layout);
  assert(!UnrealResolveFloatPropertyOffset(binding,L"_bIconicMode",&eye_offset));
  assert(!UnrealResolveFloatPropertyOffset(binding,L"MissingHeight",&eye_offset));
  assert(!UnrealResolveFloatPropertyOffset(nullptr,L"BaseEyeHeight",&eye_offset));
  assert(!UnrealResolveFloatPropertyOffset(binding,nullptr,&eye_offset));
  assert(!UnrealResolveFloatPropertyOffset(binding,L"BaseEyeHeight",nullptr));
  // Capacity retuning uses a typed scalar CAS, never an unchecked write into
  // a cached-readable page or a neighboring field/array element.
  void* ammo_field=MakeObject(L"AmmoIncrease",11,classes[8]);Link(classes[8],ammo_field);
  const uint32_t ammo_layout=1 | (4<<10) | (0x128<<20);
  Put(ammo_field,0x30,ammo_layout);ClearWorldReflectionCaches();
  Put(binding,0x128,int32_t{6});
  assert(!UnrealWriteIntPropertyExpected(binding,L"AmmoIncrease",5,60));
  assert(UnrealWriteIntPropertyExpected(binding,L"AmmoIncrease",6,60));
  assert(UnrealWriteIntPropertyExpected(binding,L"AmmoIncrease",60,60));
  for(auto layout:{uint32_t{2 | (4<<10) | (0x128<<20)},uint32_t{1 | (8<<10) | (0x128<<20)},
      uint32_t{1 | (4<<10) | (0x129<<20)},uint32_t{1 | (4<<10) | (0x18<<20)}}){
    Put(ammo_field,0x30,layout);ClearWorldReflectionCaches();
    assert(!UnrealWriteIntPropertyExpected(binding,L"AmmoIncrease",60,6));
  }
  Put(ammo_field,0x30,ammo_layout);ClearWorldReflectionCaches();
  assert(!UnrealWriteIntPropertyExpected(binding,L"_bIconicMode",0,1));
  assert(!UnrealWriteIntPropertyExpected(binding,nullptr,60,6));
  assert(!UnrealWriteIntPropertyExpected(nullptr,L"AmmoIncrease",60,6));
  g_reference_write_thread=GetCurrentThreadId()+1;
  assert(!UnrealWriteIntPropertyExpected(binding,L"AmmoIncrease",60,6));
  g_reference_write_thread=GetCurrentThreadId();
  protected_object=static_cast<uint8_t*>(VirtualAlloc(nullptr,page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(protected_object);
  std::memcpy(protected_object,binding,sizeof(Object));object_slots[binding_index-1]=protected_object;ClearWorldReflectionCaches();
  assert(Readable(protected_object,sizeof(Object)));
  assert(VirtualProtect(protected_object,page,PAGE_READONLY,&old));
  assert(!UnrealWriteIntPropertyExpected(protected_object,L"AmmoIncrease",60,60));
  assert(!UnrealWriteIntPropertyExpected(protected_object,L"AmmoIncrease",60,6));
  assert(VirtualProtect(protected_object,page,PAGE_READWRITE,&old));
  assert(UnrealWriteIntPropertyExpected(protected_object,L"AmmoIncrease",60,6));
  object_slots[binding_index-1]=binding;ClearWorldReflectionCaches();assert(VirtualFree(protected_object,0,MEM_RELEASE));
  ClearReflectionCaches();
  classes[17]=MakeObject(L"NameProperty",1);
  void* service_definition=MakeObject(L"TnServiceDescription",3);
  void* service_type=MakeObject(L"Type",17,service_definition);
  Link(service_definition,service_type);
  Put(service_type,0x30,uint32_t{1 | (4 << 10)});
  struct ServiceRow { uint32_t name; void* service; } row{Name(L"TnBuffService"),binding};
  UnrealStructArrayView service_rows{&row,1,1,sizeof(row),service_definition};
  bool matches=false;
  assert(UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",&matches) && matches);
  row.name|=1; // Same display/base name, DIFFERENT packed FName number.
  assert(UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",&matches) && !matches);
  row.name=Name(L"OtherService");
  assert(UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",&matches) && !matches);
  for (uint32_t layout : {uint32_t{2 | (4 << 10)},uint32_t{1 | (8 << 10)},uint32_t{1 | (4 << 10) | (8 << 20)}}) {
    Put(service_type,0x30,layout); matches=true;
    assert(!UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",&matches) && !matches);
  }
  Put(service_type,0x30,uint32_t{1 | (4 << 10)});
  assert(!UnrealStructArrayNameEquals(service_rows,1,L"Type",L"TnBuffService",&matches));
  assert(!UnrealStructArrayNameEquals(service_rows,0,L"Missing",L"TnBuffService",&matches));
  assert(!UnrealStructArrayNameEquals(service_rows,0,L"Type",nullptr,&matches));
  assert(!UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",nullptr));
  service_rows.data=nullptr;
  assert(!UnrealStructArrayNameEquals(service_rows,0,L"Type",L"TnBuffService",&matches));
  ClearReflectionCaches();
  void* resolved_class=nullptr;
  assert(UnrealResolveClassByPath(L"TransGame.OtherGame",&resolved_class) && resolved_class==classes[8]);
  assert(!UnrealResolveClassByPath(L"OtherGame",&resolved_class) && !resolved_class);
  assert(!UnrealResolveClassByPath(L"TransGame.MissingClass",&resolved_class) && !resolved_class);
  assert(!UnrealResolveClassByPath(nullptr,&resolved_class) && !resolved_class);
  assert(!UnrealResolveClassByPath(L"TransGame.OtherGame",nullptr));
  // The native collision exception is exact-owner/name/flags only. Do not
  // accidentally enable the identically named stripped TransGame wrapper or
  // any new native functions on another component class.
  void* engine_package=MakeObject(L"Engine",6);
  void* primitive_class=MakeObject(L"PrimitiveComponent",1,engine_package);
  void* collision_function=MakeObject(L"SetRBCollidesWithChannel",4,primitive_class);
  Script(collision_function);Put(collision_function,0x54,uint32_t{0x00020400});
  assert(ValidCallableFunction(collision_function));
  Put(collision_function,0x58,uint16_t{1});assert(!ValidCallableFunction(collision_function));
  Put(collision_function,0x58,uint16_t{0});
  Put(collision_function,0x54,uint32_t{0x02020400});assert(!ValidCallableFunction(collision_function));
  Put(collision_function,0x54,uint32_t{0x00020400});
  Put(primitive_class,4,Name(L"WrongComponent"));assert(!ValidCallableFunction(collision_function));
  Put(primitive_class,4,Name(L"PrimitiveComponent"));
  Put(engine_package,4,Name(L"TransGame"));assert(!ValidCallableFunction(collision_function));
  Put(engine_package,4,Name(L"Engine"));
  // The packed shipping class can omit its package outer. Admit that exact
  // leaf, but never a conflicting package that is actually represented.
  const auto primitive_identity=*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(primitive_class)+0x14);
  Put(primitive_class,0x14,uint32_t{1});assert(ValidCallableFunction(collision_function));
  Put(primitive_class,0x14,primitive_identity);
  Put(collision_function,4,Name(L"SetSomethingElse"));assert(!ValidCallableFunction(collision_function));
  Put(collision_function,4,Name(L"SetRBCollidesWithChannel"));
  Put(collision_function,0x68,static_cast<void*>(nullptr));assert(!ValidCallableFunction(collision_function));
  Put(collision_function,0x68,reinterpret_cast<void*>(1));assert(!ValidCallableFunction(collision_function));
  Script(collision_function);Put(collision_function,0x54,uint32_t{0x00020400});
  Put(collision_function,0x5D,uint8_t{2});Put(collision_function,0x5E,uint16_t{8});
  void* collision_channel=MakeObject(L"Channel",14,collision_function);
  void* collision_value=MakeObject(L"bNewCollides",15,collision_function);
  Put(collision_channel,0x30,uint32_t{1 | (1<<10)});
  Put(collision_value,0x30,uint32_t{1 | (4<<10) | (4<<20)});
  Put(collision_channel,0x38,uint32_t{kPropertyParm});Put(collision_value,0x38,uint32_t{kPropertyParm});
  Put(collision_value,0x40,uint32_t{0x80000000});
  Link(collision_function,collision_channel);Put(collision_channel,0x2C,collision_value);
  classes[18]=primitive_class;collision_receiver=MakeObject(L"CollisionMesh",18);
  engine_result=collision_function;g_invoke=reinterpret_cast<void*>(&InvokeTestCollision);
  assert(UnrealValidateCallContract(collision_receiver,L"SetRBCollidesWithChannel",nullptr,
      {{L"Channel",L"ByteProperty",1},{L"bNewCollides",L"BoolProperty",4}}));
  for(unsigned i=0;i<3000;++i) {
    collision_mask=(i&1) ? 0x80000000:0;
    assert(UnrealCallVoidByteBool(collision_receiver,L"SetRBCollidesWithChannel",L"Channel",15,L"bNewCollides",(i&1)!=0));
  }
  assert(collision_calls==3000);
  assert(!UnrealCallVoidByteBool(collision_receiver,L"SetRBCollidesWithChannel",L"Channel",15,L"Value",true));
  assert(collision_calls==3000);
  TestObjectClones(argc>1 ? argv[1]:nullptr);
  std::puts("UnrealBridgeLookupTests passed (including rooted clone-array allocation/rollback)");
}
