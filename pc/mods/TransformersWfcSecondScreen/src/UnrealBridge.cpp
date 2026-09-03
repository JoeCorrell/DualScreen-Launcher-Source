#include "UnrealBridge.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>

#include "Log.h"

namespace wfcds {
namespace {

constexpr uintptr_t kGObjectsRva = 0x0222F0A4u;
constexpr uintptr_t kGNamesRva = 0x0222F0F4u;
constexpr uintptr_t kClassTableRva = 0x02129FD4u;
constexpr uintptr_t kMakeFNameRva = 0x00DCC800u;
constexpr uintptr_t kInvokeRva = 0x00DE6900u;
constexpr uintptr_t kInitializeUFunctionRva = 0x00DE5E20u;
constexpr uintptr_t kFStringAssignRva = 0x00001150u;
constexpr size_t kFindFunctionVtableOffset = 0xC0u;
constexpr size_t kMaxCallParameters = 1024;
constexpr int32_t kMaxObjects = 500000;
constexpr int32_t kMaxNames = 262143;
// A live snapshot touches objects from many UE3 allocations. Keeping enough
// committed regions for the whole pulse avoids repeating VirtualQuery hundreds
// of times under Wine/Box64, where it is particularly expensive.
constexpr size_t kMemoryRegionCacheCapacity = 128;
// Snapshot/property vocabulary is a little over 128 names before class names
// are included.  The old pointer-keyed cache filled during the first live
// snapshot and then called TWFC's MakeFName for every uncached lookup. Keep a
// power-of-two, content-keyed table large enough for the complete bridge.
constexpr size_t kNameCacheCapacity = 512;
constexpr size_t kLookupCacheCapacity = 1024;
constexpr size_t kMaxOuterDepth = 64;
constexpr size_t kMaxFunctionExecDetours = 64;
constexpr size_t kMaxScriptObjectOperandPatches = 32;
constexpr size_t kFunctionExecOffset = 0x68u;
constexpr size_t kFunctionScriptDataOffset = 0x38u;
constexpr size_t kFunctionScriptCountOffset = 0x3Cu;
constexpr size_t kFunctionScriptCapacityOffset = 0x40u;
constexpr int32_t kMaxScriptBytes = 1 << 20;
constexpr uint32_t kFunctionDefined = 0x00000002u;
constexpr uint32_t kFunctionIterator = 0x00000004u;
constexpr uint32_t kFunctionLatent = 0x00000008u;
constexpr uint32_t kFunctionNative = 0x00000400u;
constexpr uint32_t kFunctionExecutorRejected = 0x18000000u;
constexpr uint32_t kPropertyParm = 0x00000080u;
constexpr uint32_t kPropertyOutParm = 0x00000100u;
constexpr uint32_t kPropertyReturnParm = 0x00000400u;

constexpr uint8_t kMakeFNamePrologue[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC,
    0x04, 0x01, 0x00, 0x00, 0x83, 0x3D};
constexpr uint8_t kInvokePrologue[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x53, 0x56,
    0x57, 0x8B, 0xF8, 0x8B, 0x47, 0x54};
constexpr uint8_t kInitializeUFunctionPrologue[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x56, 0x8B,
    0xF1, 0x8B, 0x46, 0x0C, 0x8B, 0xD0, 0x81, 0xE2,
    0x00, 0x00, 0x00, 0x01, 0x33, 0xC9, 0x0B, 0xCA,
    0x57, 0x8B, 0x7E, 0x08, 0x74, 0x5C};
constexpr uint8_t kFStringAssignPrologue[] = {
    0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x8B, 0xF1, 0x3B, 0xF7,
    0x74, 0x39, 0x8B, 0x47, 0x04, 0x6A, 0x02, 0x50, 0x6A, 0x00};

#pragma pack(push, 1)
struct UnrealArray {
  void** data;
  int32_t count;
  int32_t capacity;
};
#pragma pack(pop)

using MakeFNameFn = void(__attribute__((stdcall)) *)(
    uint32_t*, const wchar_t*, uint32_t, uint32_t, uint32_t);
// The target is a no-argument thiscall. A two-register fastcall prototype is
// ABI-equivalent here: UFunction is supplied in ECX and the unused EDX value is
// caller-scratch; there are no stack arguments to clean up.
using InitializeUFunctionFn = void(WFCDS_UNREAL_EXEC_CALL *)(void*, void*);
extern "C" void WfcInvokeReflected(void* executor, void* function,
                                     void* object, void* parameters);
extern "C" void* WfcFindFunction(void* function, void* object,
                                   uint32_t packed_name, uint32_t reserved);
extern "C" void* WfcAssignFString(void* function, void* destination,
                                    const void* source);

HMODULE g_executable = nullptr;
UnrealArray* g_objects = nullptr;
UnrealArray* g_names = nullptr;
void** g_class_table = nullptr;
MakeFNameFn g_make_name = nullptr;
InitializeUFunctionFn g_initialize_ufunction = nullptr;
void* g_invoke = nullptr;
void* g_fstring_assign = nullptr;
bool g_ready = false;

void WFCDS_UNREAL_EXEC_CALL SuppressedIdentitySideEffect(
    void*, void*, void*, void*) {}

struct MemoryRegionCacheEntry {
  uintptr_t start = 0;
  uintptr_t end = 0;
};

struct NameCacheEntry {
  const wchar_t* literal = nullptr;
  uint32_t packed = 0;
};

struct LookupCacheEntry {
  void* context = nullptr;
  uint32_t name_index = 0;
  void* result = nullptr;
};

struct IsACacheEntry {
  void* object_class = nullptr;
  uint32_t target_name_index = 0;
  bool result = false;
};

std::array<MemoryRegionCacheEntry, kMemoryRegionCacheCapacity>
    g_memory_region_cache = {};
size_t g_memory_region_cache_count = 0;
std::array<NameCacheEntry, kNameCacheCapacity> g_name_cache = {};
size_t g_name_cache_count = 0;
std::array<LookupCacheEntry, kLookupCacheCapacity> g_field_cache = {};
size_t g_field_cache_count = 0;
std::array<LookupCacheEntry, kLookupCacheCapacity> g_struct_field_cache = {};
size_t g_struct_field_cache_count = 0;
std::array<LookupCacheEntry, kLookupCacheCapacity> g_call_field_cache = {};
size_t g_call_field_cache_count = 0;
std::array<IsACacheEntry, kLookupCacheCapacity> g_is_a_cache = {};
size_t g_is_a_cache_count = 0;

struct ActiveFunctionExecDetour {
  UnrealFunctionHandle function = {};
  UnrealFunctionExec original_executor = nullptr;
  UnrealFunctionExec replacement_executor = nullptr;
};

struct ActiveScriptObjectOperandPatch {
  UnrealScriptObjectOperandPatch patch = {};
};

std::array<ActiveFunctionExecDetour, kMaxFunctionExecDetours>
    g_function_exec_detours = {};
size_t g_function_exec_detour_count = 0;
std::array<ActiveScriptObjectOperandPatch, kMaxScriptObjectOperandPatches>
    g_script_object_operand_patches = {};
size_t g_script_object_operand_patch_count = 0;

void ClearWorldReflectionCaches() {
  g_memory_region_cache_count = 0;
  g_field_cache.fill({});
  g_field_cache_count = 0;
  g_struct_field_cache.fill({});
  g_struct_field_cache_count = 0;
  g_call_field_cache.fill({});
  g_call_field_cache_count = 0;
  g_is_a_cache.fill({});
  g_is_a_cache_count = 0;
}

void ClearReflectionCaches() {
  ClearWorldReflectionCaches();
  g_name_cache.fill({});
  g_name_cache_count = 0;
}

size_t NameHash(const wchar_t* text) {
  size_t hash = sizeof(size_t) == 8
                    ? static_cast<size_t>(1469598103934665603ull)
                    : static_cast<size_t>(2166136261u);
  const size_t prime = sizeof(size_t) == 8
                           ? static_cast<size_t>(1099511628211ull)
                           : static_cast<size_t>(16777619u);
  for (; text && *text; ++text) {
    hash ^= static_cast<size_t>(*text);
    hash *= prime;
  }
  return hash;
}

size_t LookupHash(void* context, uint32_t name_index) {
  size_t value = reinterpret_cast<size_t>(context) >> 4;
  value ^= static_cast<size_t>(name_index) * static_cast<size_t>(0x9E3779B1u);
  value ^= value >> (sizeof(size_t) == 8 ? 33 : 16);
  return value;
}

bool FindCachedLookup(
    const std::array<LookupCacheEntry, kLookupCacheCapacity>& cache,
    size_t count, void* context, uint32_t name_index, void** result) {
  if (!count || !context || !result) return false;
  const size_t start = LookupHash(context, name_index) & (cache.size() - 1);
  for (size_t probe = 0; probe < cache.size(); ++probe) {
    const LookupCacheEntry& entry = cache[(start + probe) & (cache.size() - 1)];
    if (!entry.context) return false;
    if (entry.context == context && entry.name_index == name_index) {
      *result = entry.result;
      return true;
    }
  }
  return false;
}

void CacheLookup(std::array<LookupCacheEntry, kLookupCacheCapacity>* cache,
                 size_t* count, void* context, uint32_t name_index,
                 void* result) {
  if (!cache || !count || !context) return;
  const size_t start = LookupHash(context, name_index) & (cache->size() - 1);
  for (size_t probe = 0; probe < cache->size(); ++probe) {
    LookupCacheEntry& entry = (*cache)[(start + probe) & (cache->size() - 1)];
    if (!entry.context) {
      entry = {context, name_index, result};
      ++*count;
      return;
    }
    if (entry.context == context && entry.name_index == name_index) {
      entry.result = result;
      return;
    }
  }
}

bool Readable(const void* pointer, size_t bytes) {
  if (!pointer || !bytes) return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
  const uintptr_t end = start + bytes;
  if (end < start) return false;
  for (size_t index = 0; index < g_memory_region_cache_count; ++index) {
    if (start >= g_memory_region_cache[index].start &&
        end <= g_memory_region_cache[index].end)
      return true;
  }
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(pointer, &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
      (info.Protect & PAGE_NOACCESS))
    return false;
  const uintptr_t region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) +
                               info.RegionSize;
  if (region_end < reinterpret_cast<uintptr_t>(info.BaseAddress) ||
      end > region_end)
    return false;
  if (g_memory_region_cache_count < g_memory_region_cache.size()) {
    g_memory_region_cache[g_memory_region_cache_count++] = {
        reinterpret_cast<uintptr_t>(info.BaseAddress), region_end};
  }
  return true;
}

bool Executable(const void* pointer) {
  MEMORY_BASIC_INFORMATION info = {};
  if (!pointer || !VirtualQuery(pointer, &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
    return false;
  const DWORD protection = info.Protect & 0xFF;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

bool ExactModuleCodeMatches(HMODULE module, const void* code,
                            const uint8_t* expected, size_t expected_size) {
  if (!module || module != GetModuleHandleW(nullptr) || !code || !expected ||
      !expected_size || !Readable(code, expected_size) || !Executable(code))
    return false;
  MEMORY_BASIC_INFORMATION info = {};
  return VirtualQuery(code, &info, sizeof(info)) &&
         info.AllocationBase == module &&
         std::memcmp(code, expected, expected_size) == 0;
}

bool ArraySane(const UnrealArray* array, int32_t maximum) {
  if (!Readable(array, sizeof(*array)) || array->count <= 0 ||
      array->count > maximum || array->capacity < array->count ||
      array->capacity > maximum)
    return false;
  return Readable(array->data,
                  static_cast<size_t>(array->count) * sizeof(void*));
}

uint32_t PackedName(void* object) {
  if (!Readable(object, 8)) return 0;
  return *reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(object) + 4);
}

const wchar_t* NameText(uint32_t packed_name) {
  if (!ArraySane(g_names, kMaxNames)) return nullptr;
  const uint32_t name_index = packed_name >> 14;
  if (name_index >= static_cast<uint32_t>(g_names->count)) return nullptr;
  void* entry = g_names->data[name_index];
  if (!Readable(entry, 10)) return nullptr;
  const wchar_t* text = reinterpret_cast<const wchar_t*>(
      static_cast<uint8_t*>(entry) + 8);
  for (size_t index = 0; index < 256; ++index) {
    if (!Readable(text + index, sizeof(wchar_t))) return nullptr;
    if (!text[index]) return text;
  }
  return nullptr;
}

bool BaseNameIndexEquals(void* object, uint32_t expected_name_index) {
  return Readable(object, 8) &&
         (PackedName(object) >> 14) == expected_name_index;
}

void* ClassOf(void* object) {
  if (!Readable(object, 0x18)) return nullptr;
  const uint32_t class_index =
      *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object) + 0x14) &
      0xFFFu;
  if (!class_index ||
      !Readable(g_class_table + class_index, sizeof(void*))) return nullptr;
  return class_index ? g_class_table[class_index] : nullptr;
}

void* ClassParent(void* class_object) {
  if (!Readable(class_object, 0x2C)) return nullptr;
  return *reinterpret_cast<void**>(
      static_cast<uint8_t*>(class_object) + 0x28);
}

void* ObjectByInternalIndex(uint32_t one_based_index) {
  if (!one_based_index || !ArraySane(g_objects, kMaxObjects) ||
      one_based_index > static_cast<uint32_t>(g_objects->count))
    return nullptr;
  return g_objects->data[one_based_index - 1];
}

void* ObjectOuter(void* object) {
  if (!Readable(object, 0x18)) return nullptr;
  // High Moon packs the twelve-bit class-table index and the eighteen-bit,
  // one-based Outer GObjects index into UObject+0x14. The same extraction is
  // used by the target executable's UField/UFunction linker at 0x116E5E46.
  const uint32_t identity = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(object) + 0x14);
  return ObjectByInternalIndex((identity >> 12) & 0x3FFFFu);
}

bool NameEqualsPathSegment(void* object, const wchar_t* begin,
                           const wchar_t* end) {
  if (!object || !begin || !end || end <= begin) return false;
  const wchar_t* actual = NameText(PackedName(object));
  if (!actual) return false;
  const size_t expected_length = static_cast<size_t>(end - begin);
  const size_t actual_length = std::wcslen(actual);
  if (actual_length != expected_length || expected_length > INT_MAX)
    return false;
  return CompareStringOrdinal(actual, static_cast<int>(actual_length), begin,
                              static_cast<int>(expected_length), TRUE) ==
         CSTR_EQUAL;
}

bool ObjectPathMatches(void* object, const wchar_t* full_path) {
  if (!UnrealObjectValid(object) || !full_path || !*full_path) return false;
  const size_t path_length = std::wcslen(full_path);
  if (!path_length || path_length > 512 || full_path[0] == L'.' ||
      full_path[path_length - 1] == L'.')
    return false;

  const wchar_t* segment_end = full_path + path_length;
  void* current = object;
  for (size_t depth = 0; depth < kMaxOuterDepth; ++depth) {
    if (!UnrealObjectValid(current)) return false;
    const wchar_t* segment_begin = segment_end;
    while (segment_begin > full_path && segment_begin[-1] != L'.')
      --segment_begin;
    if (!NameEqualsPathSegment(current, segment_begin, segment_end))
      return false;
    current = ObjectOuter(current);
    if (segment_begin == full_path) return current == nullptr;
    if (segment_begin == full_path + 1 || segment_begin[-1] != L'.')
      return false;
    segment_end = segment_begin - 1;
  }
  return false;
}

uint64_t ObjectPathIdentityHash(void* object) {
  constexpr uint64_t kOffset = 1469598103934665603ull;
  constexpr uint64_t kPrime = 1099511628211ull;
  if (!UnrealObjectValid(object)) return 0;
  uint64_t hash = kOffset;
  size_t depth = 0;
  for (void* current = object; current && depth < kMaxOuterDepth;
       current = ObjectOuter(current), ++depth) {
    if (!UnrealObjectValid(current)) return 0;
    hash ^= static_cast<uint64_t>(PackedName(current) >> 14);
    hash *= kPrime;
    hash ^= static_cast<uint64_t>(UnrealObjectIndex(current));
    hash *= kPrime;
  }
  if (depth == kMaxOuterDepth || !depth) return 0;
  hash ^= static_cast<uint64_t>(depth);
  hash *= kPrime;
  return hash ? hash : 1;
}

bool MakeName(const wchar_t* text, uint32_t* output) {
  if (!g_ready || !text || !*text || !output) return false;
  const size_t start = NameHash(text) & (g_name_cache.size() - 1);
  for (size_t probe = 0; probe < g_name_cache.size(); ++probe) {
    const NameCacheEntry& entry =
        g_name_cache[(start + probe) & (g_name_cache.size() - 1)];
    if (!entry.literal) break;
    if (entry.literal == text || std::wcscmp(entry.literal, text) == 0) {
      *output = entry.packed;
      return true;
    }
  }
  *output = 0;
  // findType=0 is the engine's non-mutating FNAME_Find path.
  g_make_name(output, text, 0, 0, 1);
  if (!*output) return false;
  for (size_t probe = 0; probe < g_name_cache.size(); ++probe) {
    NameCacheEntry& entry =
        g_name_cache[(start + probe) & (g_name_cache.size() - 1)];
    if (!entry.literal) {
      entry = {text, *output};
      ++g_name_cache_count;
      break;
    }
  }
  return true;
}

void* FindFunction(void* object, const wchar_t* name) {
  if (!UnrealObjectValid(object)) return nullptr;
  uint32_t packed = 0;
  if (!MakeName(name, &packed)) return nullptr;
  void* object_class = ClassOf(object);
  if (!object_class) return nullptr;
  void** vtable = *reinterpret_cast<void***>(object);
  if (!Readable(vtable, kFindFunctionVtableOffset + sizeof(void*)))
    return nullptr;
  void* address = vtable[kFindFunctionVtableOffset / sizeof(void*)];
  if (!Executable(address)) return nullptr;
  void* function = WfcFindFunction(address, object, packed, 0);
  if (!Readable(function, 0x6C) || !UnrealObjectValid(function) ||
      !UnrealObjectIsA(function, L"Function") ||
      !BaseNameIndexEquals(function, packed >> 14))
    function = nullptr;
  return function;
}

void* FindField(void* owner, const wchar_t* field_name) {
  if (!UnrealObjectValid(owner) || !field_name) return nullptr;
  uint32_t packed = 0;
  if (!MakeName(field_name, &packed)) return nullptr;
  void* current_class = ClassOf(owner);
  if (!current_class) return nullptr;
  const uint32_t name_index = packed >> 14;
  void* cached = nullptr;
  if (FindCachedLookup(g_field_cache, g_field_cache_count, current_class,
                       name_index, &cached))
    return cached;
  void* owner_class = current_class;
  for (int depth = 0; current_class && depth < 128; ++depth) {
    if (!Readable(current_class, 0x34)) return nullptr;
    void* field = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(current_class) + 0x30);
    for (int count = 0; field && count < 4096; ++count) {
      if (!Readable(field, 0x40)) return nullptr;
      if (BaseNameIndexEquals(field, name_index)) {
        CacheLookup(&g_field_cache, &g_field_cache_count, owner_class,
                    name_index, field);
        return field;
      }
      field = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x2C);
    }
    current_class = ClassParent(current_class);
  }
  CacheLookup(&g_field_cache, &g_field_cache_count, owner_class, name_index,
              nullptr);
  return nullptr;
}

void* FindStructField(void* struct_definition, const wchar_t* field_name) {
  if (!UnrealObjectValid(struct_definition) || !field_name) return nullptr;
  uint32_t packed = 0;
  if (!MakeName(field_name, &packed)) return nullptr;
  const uint32_t name_index = packed >> 14;
  void* cached = nullptr;
  if (FindCachedLookup(g_struct_field_cache, g_struct_field_cache_count,
                       struct_definition, name_index, &cached))
    return cached;
  void* current = struct_definition;
  for (int depth = 0; current && depth < 128; ++depth) {
    if (!Readable(current, 0x34)) return nullptr;
    void* field = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(current) + 0x30);
    for (int count = 0; field && count < 4096; ++count) {
      if (!Readable(field, 0x44)) return nullptr;
      if (BaseNameIndexEquals(field, name_index)) {
        CacheLookup(&g_struct_field_cache, &g_struct_field_cache_count,
                    struct_definition, name_index, field);
        return field;
      }
      field = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x2C);
    }
    current = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(current) + 0x28);
  }
  CacheLookup(&g_struct_field_cache, &g_struct_field_cache_count,
              struct_definition, name_index, nullptr);
  return nullptr;
}

uint32_t FieldOffset(void* field) {
  if (!Readable(field, 0x34)) return UINT32_MAX;
  return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(field) + 0x30) >>
         20;
}

uint32_t FieldElementSize(void* field) {
  if (!Readable(field, 0x34)) return 0;
  return (*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(field) + 0x30) >>
          10) &
         0x3FFu;
}

uint32_t FieldArrayDim(void* field) {
  if (!Readable(field, 0x34)) return 0;
  return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(field) + 0x30) &
         0x3FFu;
}

uint32_t ReflectedStructSize(void* struct_definition) {
  if (!UnrealObjectValid(struct_definition) ||
      !Readable(static_cast<uint8_t*>(struct_definition) + 0x34,
                sizeof(uint16_t)))
    return 0;
  return *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(struct_definition) + 0x34);
}

bool ValidScriptFunction(void* function) {
  if (!function || !Readable(function, 0x6C) ||
      !UnrealObjectValid(function) ||
      !UnrealObjectIsA(function, L"Function"))
    return false;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(function) + 0x54);
  const uint16_t native_token = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function) + 0x58);
  return (flags & kFunctionDefined) != 0 &&
         (flags & (kFunctionIterator | kFunctionLatent)) == 0 &&
         (flags & kFunctionNative) == 0 &&
         (flags & kFunctionExecutorRejected) == 0 && native_token == 0;
}

void* FunctionContextClass(void* owner) {
  void* current = owner;
  for (size_t depth = 0; current && depth < kMaxOuterDepth; ++depth) {
    if (!UnrealObjectValid(current)) return nullptr;
    if (UnrealObjectIsA(current, L"Class")) return current;
    if (!UnrealObjectIsA(current, L"Struct")) return nullptr;
    current = ObjectOuter(current);
  }
  return nullptr;
}

bool FunctionIdentityMatches(void* function, uint32_t function_name_index,
                             const wchar_t* full_owner_path) {
  if (!ValidScriptFunction(function) ||
      !BaseNameIndexEquals(function, function_name_index))
    return false;
  void* owner = ObjectOuter(function);
  return owner && UnrealObjectIsA(owner, L"Struct") &&
         FunctionContextClass(owner) &&
         ObjectPathMatches(owner, full_owner_path);
}

bool BuildExactFunctionHandle(void* function,
                              UnrealFunctionHandle* output) {
  if (!output) return false;
  *output = {};
  if (!ValidScriptFunction(function)) return false;
  void* owner = ObjectOuter(function);
  if (!owner || !UnrealObjectValid(owner) ||
      !UnrealObjectIsA(owner, L"Struct"))
    return false;
  void* context_class = FunctionContextClass(owner);
  if (!context_class) return false;
  const uint64_t owner_path_hash = ObjectPathIdentityHash(owner);
  if (!owner_path_hash) return false;
  output->function = function;
  output->owner = owner;
  output->context_class = context_class;
  output->function_object_index = UnrealObjectIndex(function);
  output->owner_object_index = UnrealObjectIndex(owner);
  output->context_class_object_index = UnrealObjectIndex(context_class);
  output->function_name_index = PackedName(function) >> 14;
  output->owner_path_hash = owner_path_hash;
  return output->function_object_index && output->owner_object_index &&
         output->context_class_object_index;
}

bool ExactFunctionHandleValid(const UnrealFunctionHandle& function) {
  return function.function && function.owner && function.context_class &&
         function.function_object_index && function.owner_object_index &&
         function.context_class_object_index &&
         function.function_name_index && function.owner_path_hash &&
         ValidScriptFunction(function.function) &&
         UnrealObjectValid(function.owner) &&
         UnrealObjectIsA(function.owner, L"Struct") &&
         UnrealObjectValid(function.context_class) &&
         UnrealObjectIsA(function.context_class, L"Class") &&
         UnrealObjectIndex(function.function) ==
             function.function_object_index &&
         UnrealObjectIndex(function.owner) == function.owner_object_index &&
         UnrealObjectIndex(function.context_class) ==
             function.context_class_object_index &&
         BaseNameIndexEquals(function.function,
                             function.function_name_index) &&
         ObjectOuter(function.function) == function.owner &&
         FunctionContextClass(function.owner) == function.context_class &&
         ObjectPathIdentityHash(function.owner) == function.owner_path_hash;
}

bool ContextImplementsFunctionOwner(
    void* object, const UnrealFunctionHandle& function) {
  if (!UnrealObjectValid(object)) return false;
  void* current = ClassOf(object);
  for (size_t depth = 0; current && depth < kMaxOuterDepth; ++depth) {
    if (current == function.context_class) return true;
    current = ClassParent(current);
  }
  return false;
}

uint8_t FunctionParameterCount(void* function) {
  return ValidScriptFunction(function)
             ? *reinterpret_cast<const uint8_t*>(
                   static_cast<const uint8_t*>(function) + 0x5D)
             : 0xFFu;
}

bool ValidInputParameter(void* field) {
  if (!field || !Readable(static_cast<const uint8_t*>(field) + 0x38,
                          sizeof(uint32_t)))
    return false;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(field) + 0x38);
  return (flags & (kPropertyParm | kPropertyOutParm |
                   kPropertyReturnParm)) == kPropertyParm;
}

struct ReflectedCall {
  void* function = nullptr;
  alignas(16) std::array<uint8_t, kMaxCallParameters> parameters = {};
  uint16_t size = 0;
  uint16_t return_offset = 0xFFFFu;
};

void* FindCallField(const ReflectedCall& call, const wchar_t* field_name);

bool PrepareCall(void* object, const wchar_t* function_name,
                 ReflectedCall* call) {
  if (!call) return false;
  *call = {};
  call->return_offset = 0xFFFFu;
  call->function = FindFunction(object, function_name);
  if (!ValidScriptFunction(call->function)) return false;
  call->size = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x5E);
  call->return_offset = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x60);
  return call->size <= call->parameters.size();
}

bool PrepareExactCall(void* object, const UnrealFunctionHandle& function,
                      ReflectedCall* call) {
  if (!call) return false;
  *call = {};
  call->return_offset = 0xFFFFu;
  if (!ExactFunctionHandleValid(function) ||
      !ContextImplementsFunctionOwner(object, function))
    return false;
  call->function = function.function;
  call->size = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x5E);
  call->return_offset = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x60);
  return call->size <= call->parameters.size();
}

bool SetIntParameter(ReflectedCall* call, const wchar_t* parameter_name,
                     int32_t value) {
  if (!call || !call->function || !parameter_name) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"IntProperty") ||
      !ValidInputParameter(field))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (offset == UINT32_MAX || size != sizeof(value) ||
      FieldArrayDim(field) != 1 ||
      offset + sizeof(value) > call->size)
    return false;
  std::memcpy(call->parameters.data() + offset, &value, sizeof(value));
  return true;
}

bool SetByteParameter(ReflectedCall* call, const wchar_t* parameter_name,
                      uint8_t value) {
  if (!call || !call->function || !parameter_name) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"ByteProperty") ||
      !ValidInputParameter(field))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (offset == UINT32_MAX || size != sizeof(value) ||
      offset + sizeof(value) > call->size)
    return false;
  std::memcpy(call->parameters.data() + offset, &value, sizeof(value));
  return true;
}

bool SetFloatParameter(ReflectedCall* call, const wchar_t* parameter_name,
                       float value) {
  if (!call || !call->function || !parameter_name) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"FloatProperty") ||
      !ValidInputParameter(field))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (offset == UINT32_MAX || size != sizeof(value) ||
      FieldArrayDim(field) != 1 || offset + sizeof(value) > call->size)
    return false;
  std::memcpy(call->parameters.data() + offset, &value, sizeof(value));
  return true;
}

bool SetObjectParameter(ReflectedCall* call, const wchar_t* parameter_name,
                        void* value) {
  if (!call || !call->function || !parameter_name ||
      (value && !UnrealObjectValid(value))) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty") ||
      !ValidInputParameter(field))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || FieldElementSize(field) != sizeof(value) ||
      offset + sizeof(value) > call->size)
    return false;
  std::memcpy(call->parameters.data() + offset, &value, sizeof(value));
  return true;
}

bool SetBoolParameter(ReflectedCall* call, const wchar_t* parameter_name,
                      bool value) {
  if (!call || !call->function || !parameter_name) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty") ||
      !ValidInputParameter(field))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(uint32_t) > call->size)
    return false;
  if (!Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t mask = *reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(field) + 0x40);
  uint32_t current = 0;
  std::memcpy(&current, call->parameters.data() + offset, sizeof(current));
  current = value ? current | mask : current & ~mask;
  std::memcpy(call->parameters.data() + offset, &current, sizeof(current));
  return true;
}

bool SetStructParameter(ReflectedCall* call, const wchar_t* parameter_name,
                        const UnrealStructValueView& value) {
  if (!call || !call->function || !parameter_name || !value.data ||
      !value.size || !UnrealObjectValid(value.struct_definition)) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !ValidInputParameter(field) ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (definition != value.struct_definition || size != value.size ||
      ReflectedStructSize(definition) != size ||
      offset == UINT32_MAX || offset + size > call->size ||
      !Readable(value.data, size))
    return false;
  std::memcpy(call->parameters.data() + offset, value.data, size);
  return true;
}

bool Execute(void* object, ReflectedCall* call) {
  if (!call || !ValidScriptFunction(call->function) ||
      !UnrealObjectValid(object) || !g_ready || !g_invoke)
    return false;
  // g_invoke is an immutable RVA in the supported TWFC executable. Startup
  // already validates its exact prologue before publishing g_ready, and
  // shutdown clears g_ready before nulling the pointer. Re-running
  // VirtualQuery for every reflected call is therefore redundant and is
  // especially costly through Wine/Box64 on the four-Hz role-proxy path.
  WfcInvokeReflected(g_invoke, call->function, object,
                     call->parameters.data());
  return true;
}

static_assert(sizeof(uintptr_t) == sizeof(LONG),
              "The WFC UFunction executor bridge requires a 32-bit build");
static_assert(sizeof(UnrealFunctionExec) == sizeof(LONG),
              "Unexpected WFC UFunction executor pointer size");

LONG ExecutorBits(UnrealFunctionExec executor) {
  LONG bits = 0;
  std::memcpy(&bits, &executor, sizeof(bits));
  return bits;
}

UnrealFunctionExec ExecutorFromBits(LONG bits) {
  UnrealFunctionExec executor = nullptr;
  std::memcpy(&executor, &bits, sizeof(executor));
  return executor;
}

bool ExecutableExecutor(UnrealFunctionExec executor) {
  const auto address = static_cast<uintptr_t>(
      static_cast<uint32_t>(ExecutorBits(executor)));
  return address && Executable(reinterpret_cast<const void*>(address));
}

volatile LONG* FunctionExecutorSlot(
    const UnrealFunctionHandle& function) {
  if (!ExactFunctionHandleValid(function) ||
      !Readable(static_cast<uint8_t*>(function.function) +
                    kFunctionExecOffset,
                sizeof(LONG)))
    return nullptr;
  return reinterpret_cast<volatile LONG*>(
      static_cast<uint8_t*>(function.function) + kFunctionExecOffset);
}

bool EnsureFunctionExecutorInitialized(
    const UnrealFunctionHandle& function, volatile LONG** output_slot,
    UnrealFunctionExec* output_executor) {
  if (!output_slot || !output_executor) return false;
  *output_slot = nullptr;
  *output_executor = nullptr;
  volatile LONG* slot = FunctionExecutorSlot(function);
  if (!slot) return false;

  LONG executor_bits = InterlockedCompareExchange(slot, 0, 0);
  UnrealFunctionExec executor = ExecutorFromBits(executor_bits);
  if (!executor_bits) {
    if (!g_executable || !g_initialize_ufunction) return false;
    auto* expected_initializer =
        reinterpret_cast<uint8_t*>(g_executable) +
        kInitializeUFunctionRva;
    if (reinterpret_cast<void*>(g_initialize_ufunction) !=
            expected_initializer ||
        !ExactModuleCodeMatches(g_executable, expected_initializer,
                                kInitializeUFunctionPrologue,
                                sizeof(kInitializeUFunctionPrologue))) {
      Log(LogLevel::kError,
          "[Lifecycle] exact-build UFunction initializer validation failed");
      return false;
    }

    // This is the exact no-argument thiscall used by the dispatcher at
    // TWFC.exe+0xDE6A6C when this UFunction's +0x68 Func slot is null.
    g_initialize_ufunction(function.function, nullptr);
    if (!ExactFunctionHandleValid(function)) return false;
    slot = FunctionExecutorSlot(function);
    if (!slot) return false;
    executor_bits = InterlockedCompareExchange(slot, 0, 0);
    executor = ExecutorFromBits(executor_bits);
  }
  if (!executor_bits || !ExecutableExecutor(executor)) return false;
  *output_slot = slot;
  *output_executor = executor;
  return true;
}

bool CompareExchangeFunctionExecutor(
    const UnrealFunctionHandle& function, UnrealFunctionExec expected,
    UnrealFunctionExec replacement) {
  volatile LONG* slot = FunctionExecutorSlot(function);
  if (!slot) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(const_cast<LONG*>(slot), &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
      (info.Protect & PAGE_NOACCESS))
    return false;

  DWORD previous_protection = 0;
  if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
                      PAGE_EXECUTE_READWRITE, &previous_protection))
    return false;
  const LONG expected_bits = ExecutorBits(expected);
  const LONG replacement_bits = ExecutorBits(replacement);
  const LONG observed = InterlockedCompareExchange(
      slot, replacement_bits, expected_bits);
  DWORD ignored = 0;
  if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG),
                      previous_protection, &ignored)) {
    Log(LogLevel::kError,
        "[Lifecycle] failed to restore UFunction executor page protection");
  }
  if (observed != expected_bits) return false;
  FlushInstructionCache(GetCurrentProcess(), const_cast<LONG*>(slot),
                        sizeof(LONG));
  return true;
}

int FindActiveFunctionExecDetour(
    const UnrealFunctionExecDetour& detour) {
  if (!detour.installed) return -1;
  for (size_t index = 0; index < g_function_exec_detour_count; ++index) {
    const ActiveFunctionExecDetour& active =
        g_function_exec_detours[index];
    if (active.function.function == detour.function.function &&
        active.function.function_object_index ==
            detour.function.function_object_index &&
        active.original_executor == detour.original_executor &&
        active.replacement_executor == detour.replacement_executor)
      return static_cast<int>(index);
  }
  return -1;
}

void RemoveActiveFunctionExecDetour(size_t index) {
  if (index >= g_function_exec_detour_count) return;
  for (size_t next = index + 1; next < g_function_exec_detour_count; ++next)
    g_function_exec_detours[next - 1] = g_function_exec_detours[next];
  --g_function_exec_detour_count;
  g_function_exec_detours[g_function_exec_detour_count] = {};
}

uint8_t* ScriptObjectOperandAddress(
    const UnrealFunctionHandle& function, uint32_t opcode_offset,
    uint8_t expected_opcode) {
  if (!ExactFunctionHandleValid(function) ||
      !Readable(static_cast<uint8_t*>(function.function) +
                    kFunctionScriptDataOffset,
                sizeof(void*) + sizeof(int32_t) * 2))
    return nullptr;
  auto* base = static_cast<uint8_t*>(function.function);
  void* script = *reinterpret_cast<void**>(base + kFunctionScriptDataOffset);
  const int32_t count = *reinterpret_cast<const int32_t*>(
      base + kFunctionScriptCountOffset);
  const int32_t capacity = *reinterpret_cast<const int32_t*>(
      base + kFunctionScriptCapacityOffset);
  constexpr size_t kOperandBytes = 1 + sizeof(void*);
  if (!script || count <= 0 || capacity < count ||
      capacity > kMaxScriptBytes ||
      opcode_offset > static_cast<uint32_t>(count) ||
      static_cast<size_t>(count) - opcode_offset < kOperandBytes)
    return nullptr;
  auto* opcode = static_cast<uint8_t*>(script) + opcode_offset;
  if (!Readable(opcode, kOperandBytes) || *opcode != expected_opcode)
    return nullptr;
  return opcode + 1;
}

bool ReplaceScriptObjectOperand(const UnrealFunctionHandle& function,
                                uint32_t opcode_offset,
                                uint8_t expected_opcode,
                                void* expected_object,
                                void* replacement_object) {
  uint8_t* operand = ScriptObjectOperandAddress(
      function, opcode_offset, expected_opcode);
  if (!operand) return false;
  void* observed = nullptr;
  std::memcpy(&observed, operand, sizeof(observed));
  if (observed != expected_object) return false;

  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(operand, &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
      (info.Protect & PAGE_NOACCESS))
    return false;
  DWORD previous_protection = 0;
  if (!VirtualProtect(operand, sizeof(replacement_object),
                      PAGE_EXECUTE_READWRITE, &previous_protection))
    return false;

  // Revalidate after changing protection. Calls are game-thread serialized,
  // but refusing a changed operand keeps rollback exact and fail-closed.
  std::memcpy(&observed, operand, sizeof(observed));
  const bool matched = observed == expected_object;
  if (matched)
    std::memcpy(operand, &replacement_object, sizeof(replacement_object));
  DWORD ignored = 0;
  if (!VirtualProtect(operand, sizeof(replacement_object),
                      previous_protection, &ignored)) {
    Log(LogLevel::kError,
        "[Lifecycle] failed to restore UFunction Script page protection");
  }
  if (!matched) return false;
  FlushInstructionCache(GetCurrentProcess(), operand,
                        sizeof(replacement_object));
  return true;
}

int FindActiveScriptObjectOperandPatch(
    const UnrealScriptObjectOperandPatch& patch) {
  if (!patch.installed) return -1;
  for (size_t index = 0; index < g_script_object_operand_patch_count;
       ++index) {
    const UnrealScriptObjectOperandPatch& active =
        g_script_object_operand_patches[index].patch;
    if (active.function.function == patch.function.function &&
        active.function.function_object_index ==
            patch.function.function_object_index &&
        active.opcode_offset == patch.opcode_offset &&
        active.expected_opcode == patch.expected_opcode &&
        active.original_object == patch.original_object &&
        active.replacement_object == patch.replacement_object)
      return static_cast<int>(index);
  }
  return -1;
}

void RemoveActiveScriptObjectOperandPatch(size_t index) {
  if (index >= g_script_object_operand_patch_count) return;
  for (size_t next = index + 1;
       next < g_script_object_operand_patch_count; ++next)
    g_script_object_operand_patches[next - 1] =
        g_script_object_operand_patches[next];
  --g_script_object_operand_patch_count;
  g_script_object_operand_patches[g_script_object_operand_patch_count] = {};
}

void* FindCallField(const ReflectedCall& call, const wchar_t* field_name) {
  if (!call.function || !field_name) return nullptr;
  uint32_t packed = 0;
  if (!MakeName(field_name, &packed)) return nullptr;
  const uint32_t name_index = packed >> 14;
  void* cached = nullptr;
  if (FindCachedLookup(g_call_field_cache, g_call_field_cache_count,
                       call.function, name_index, &cached))
    return cached;
  void* field = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(call.function) + 0x30);
  for (int count = 0; field && count < 128; ++count) {
    if (!Readable(field, 0x44)) return nullptr;
    if (BaseNameIndexEquals(field, name_index)) {
      CacheLookup(&g_call_field_cache, &g_call_field_cache_count,
                  call.function, name_index, field);
      return field;
    }
    field = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x2C);
  }
  CacheLookup(&g_call_field_cache, &g_call_field_cache_count, call.function,
              name_index, nullptr);
  return nullptr;
}

bool ReadBoolReturn(const ReflectedCall& call, bool* output) {
  if (!output) return false;
  void* field = FindCallField(call, L"ReturnValue");
  if (!field || !UnrealObjectIsA(field, L"BoolProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(uint32_t) > call.size) return false;
  if (!Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t mask = *reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(field) + 0x40);
  uint32_t value = 0;
  std::memcpy(&value, call.parameters.data() + offset, sizeof(value));
  *output = (value & mask) != 0;
  return true;
}

template <typename T>
bool ReadReturn(const ReflectedCall& call, T* output) {
  if (!output || call.return_offset == 0xFFFFu ||
      static_cast<size_t>(call.return_offset) + sizeof(T) > call.size)
    return false;
  std::memcpy(output, call.parameters.data() + call.return_offset, sizeof(T));
  return true;
}

template <typename T>
bool CallScalar(void* object, const wchar_t* function_name, T* output) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) && Execute(object, &call) &&
         ReadReturn(call, output);
}

template <typename T>
bool CallScalarInt(void* object, const wchar_t* function_name,
                   const wchar_t* parameter_name, int32_t value, T* output) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetIntParameter(&call, parameter_name, value) &&
         Execute(object, &call) && ReadReturn(call, output);
}

bool ReadPropertyBytes(void* object, const wchar_t* property_name,
                       const wchar_t* property_class, void* output,
                       size_t bytes) {
  if (!output || !bytes || !UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || (property_class && !UnrealObjectIsA(field, property_class)))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t element_size = FieldElementSize(field);
  if (offset == UINT32_MAX || element_size < bytes ||
      !Readable(static_cast<uint8_t*>(object) + offset, bytes))
    return false;
  std::memcpy(output, static_cast<uint8_t*>(object) + offset, bytes);
  return true;
}

bool CopyUnrealString(const UnrealArrayView& value, char* output,
                      size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (value.count < 0 || value.capacity < value.count ||
      value.count > 4096 || value.capacity > 4096) return false;
  if (value.count == 0) return true;
  if (!Readable(value.data, static_cast<size_t>(value.count) * sizeof(wchar_t)))
    return false;
  const auto* text = static_cast<const wchar_t*>(value.data);
  const int length = value.count > 0 && text[value.count - 1] == L'\0'
                         ? value.count - 1 : value.count;
  if (length == 0) return true;
  const int converted = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text, length, output,
      static_cast<int>(capacity - 1), nullptr, nullptr);
  if (!converted) return false;
  output[converted] = '\0';
  return true;
}

bool StructElement(const UnrealStructArrayView& array, int32_t index,
                   uint8_t** output) {
  if (!output || index < 0 || index >= array.count ||
      array.count > array.capacity || array.element_size == 0 ||
      array.element_size > 0x10000 ||
      !UnrealObjectValid(array.struct_definition)) return false;
  const size_t offset = static_cast<size_t>(index) * array.element_size;
  auto* element = static_cast<uint8_t*>(array.data) + offset;
  if (!Readable(element, array.element_size)) return false;
  *output = element;
  return true;
}

bool ReadStructFieldBytes(const UnrealStructArrayView& array, int32_t index,
                          const wchar_t* field_name,
                          const wchar_t* field_class, void* output,
                          size_t bytes) {
  if (!output || !bytes) return false;
  uint8_t* element = nullptr;
  if (!StructElement(array, index, &element)) return false;
  void* field = FindStructField(array.struct_definition, field_name);
  if (!field || (field_class && !UnrealObjectIsA(field, field_class)))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t element_size = FieldElementSize(field);
  if (offset == UINT32_MAX || element_size < bytes ||
      offset + bytes > array.element_size || !Readable(element + offset, bytes))
    return false;
  std::memcpy(output, element + offset, bytes);
  return true;
}

}  // namespace

bool InitializeUnrealBridge(HMODULE executable) {
  if (g_ready) return executable == g_executable;
  if (!executable) return false;
  auto* base = reinterpret_cast<uint8_t*>(executable);
  void* make_name = base + kMakeFNameRva;
  void* invoke = base + kInvokeRva;
  void* initialize_ufunction = base + kInitializeUFunctionRva;
  void* fstring_assign = base + kFStringAssignRva;
  if (!Readable(make_name, sizeof(kMakeFNamePrologue)) ||
      std::memcmp(make_name, kMakeFNamePrologue,
                  sizeof(kMakeFNamePrologue)) != 0 ||
      !Readable(invoke, sizeof(kInvokePrologue)) ||
      std::memcmp(invoke, kInvokePrologue, sizeof(kInvokePrologue)) != 0 ||
      !ExactModuleCodeMatches(executable, initialize_ufunction,
                              kInitializeUFunctionPrologue,
                              sizeof(kInitializeUFunctionPrologue)) ||
      !Readable(fstring_assign, sizeof(kFStringAssignPrologue)) ||
      std::memcmp(fstring_assign, kFStringAssignPrologue,
                  sizeof(kFStringAssignPrologue)) != 0) {
    Log(LogLevel::kError,
        "[Lifecycle] exact-build reflection signatures mismatch; bridge disabled");
    return false;
  }
  g_executable = executable;
  g_objects = reinterpret_cast<UnrealArray*>(base + kGObjectsRva);
  g_names = reinterpret_cast<UnrealArray*>(base + kGNamesRva);
  g_class_table = reinterpret_cast<void**>(base + kClassTableRva);
  g_make_name = reinterpret_cast<MakeFNameFn>(make_name);
  g_initialize_ufunction =
      reinterpret_cast<InitializeUFunctionFn>(initialize_ufunction);
  g_invoke = invoke;
  g_fstring_assign = fstring_assign;
  ClearReflectionCaches();
  g_ready = true;
  Log(LogLevel::kInfo,
      "[Lifecycle] High Moon UE3 resolver initialized names=%p objects=%p invoke=%08lX",
      static_cast<void*>(g_names), static_cast<void*>(g_objects),
      static_cast<unsigned long>(kInvokeRva));
  return true;
}

void ShutdownUnrealBridge() {
  if (g_ready && g_script_object_operand_patch_count &&
      !UnrealRestoreAllScriptObjectOperandPatches()) {
    Log(LogLevel::kError,
        "[Lifecycle] one or more UFunction Script operand patches could not be restored");
  }
  g_script_object_operand_patches.fill({});
  g_script_object_operand_patch_count = 0;
  if (g_ready && g_function_exec_detour_count &&
      !UnrealRestoreAllFunctionExecDetours()) {
    Log(LogLevel::kError,
        "[Lifecycle] one or more UFunction executor detours could not be restored");
  }
  g_function_exec_detours.fill({});
  g_function_exec_detour_count = 0;
  g_ready = false;
  ClearReflectionCaches();
  g_invoke = nullptr;
  g_fstring_assign = nullptr;
  g_make_name = nullptr;
  g_initialize_ufunction = nullptr;
  g_class_table = nullptr;
  g_names = nullptr;
  g_objects = nullptr;
  g_executable = nullptr;
}

bool UnrealBridgeReady() {
  return g_ready && ArraySane(g_objects, kMaxObjects) &&
         ArraySane(g_names, kMaxNames);
}

void UnrealBeginGameThreadPulse() {
  // VirtualQuery is exceptionally expensive under Wine/Box64. Cache committed
  // regions only for this bounded game-thread pulse so repeated validation
  // remains safe across level transitions while avoiding hundreds of
  // duplicate queries. The caller runs this before native Tick submits the
  // current frame's render-command queue.
  g_memory_region_cache_count = 0;
}

void UnrealResetWorldCaches() {
  ClearWorldReflectionCaches();
}

int32_t UnrealObjectCount() {
  return UnrealBridgeReady() ? g_objects->count : 0;
}

void* UnrealObjectAt(int32_t index) {
  if (!UnrealBridgeReady() || index < 0 || index >= g_objects->count)
    return nullptr;
  return g_objects->data[index];
}

bool UnrealObjectValid(void* object) {
  if (!object || !g_ready || !g_objects ||
      !Readable(g_objects, sizeof(*g_objects)) ||
      g_objects->count <= 0 || g_objects->count > kMaxObjects ||
      g_objects->capacity < g_objects->count ||
      g_objects->capacity > kMaxObjects || !Readable(object, 0x28))
    return false;
  const uint32_t index = UnrealObjectIndex(object);
  return index > 0 && index <= static_cast<uint32_t>(g_objects->count) &&
         Readable(g_objects->data + index - 1, sizeof(void*)) &&
         g_objects->data[index - 1] == object;
}

bool UnrealObjectIsA(void* object, const wchar_t* class_name) {
  if (!UnrealObjectValid(object) || !class_name) return false;
  uint32_t packed = 0;
  if (!MakeName(class_name, &packed)) return false;
  const uint32_t target_name_index = packed >> 14;
  void* current = ClassOf(object);
  if (!current) return false;
  void* object_class = current;
  const size_t cache_start =
      LookupHash(object_class, target_name_index) & (g_is_a_cache.size() - 1);
  for (size_t probe = 0; probe < g_is_a_cache.size(); ++probe) {
    const IsACacheEntry& entry =
        g_is_a_cache[(cache_start + probe) & (g_is_a_cache.size() - 1)];
    if (!entry.object_class) break;
    if (entry.object_class == object_class &&
        entry.target_name_index == target_name_index)
      return entry.result;
  }
  bool result = false;
  for (int depth = 0; current && depth < 128; ++depth) {
    if (BaseNameIndexEquals(current, target_name_index)) {
      result = true;
      break;
    }
    current = ClassParent(current);
  }
  for (size_t probe = 0; probe < g_is_a_cache.size(); ++probe) {
    IsACacheEntry& entry =
        g_is_a_cache[(cache_start + probe) & (g_is_a_cache.size() - 1)];
    if (!entry.object_class) {
      entry = {object_class, target_name_index, result};
      ++g_is_a_cache_count;
      break;
    }
  }
  return result;
}

bool UnrealResolveClassInHierarchy(void* object, const wchar_t* class_name,
                                   void** output) {
  if (!output) return false;
  *output = nullptr;
  if (!UnrealObjectValid(object) || !class_name || !*class_name) return false;
  uint32_t packed = 0;
  if (!MakeName(class_name, &packed)) return false;
  const uint32_t name_index = packed >> 14;
  void* current = ClassOf(object);
  for (size_t depth = 0; current && depth < kMaxOuterDepth; ++depth) {
    if (!UnrealObjectValid(current) ||
        !UnrealObjectIsA(current, L"Class"))
      return false;
    if (BaseNameIndexEquals(current, name_index)) {
      *output = current;
      return true;
    }
    current = ClassParent(current);
  }
  return false;
}

uint32_t UnrealObjectIndex(void* object) {
  if (!Readable(object, 0x1C)) return 0;
  return *reinterpret_cast<uint32_t*>(
             static_cast<uint8_t*>(object) + 0x18) &
         0x3FFFFu;
}

bool UnrealObjectName(void* object, char* output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (!UnrealObjectValid(object)) return false;
  const wchar_t* text = NameText(PackedName(object));
  if (!text) return false;
  const int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                                         -1, output,
                                         static_cast<int>(capacity), nullptr,
                                         nullptr);
  if (!result) output[0] = '\0';
  return result != 0;
}

bool UnrealObjectClassName(void* object, char* output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (!UnrealObjectValid(object)) return false;
  void* class_object = ClassOf(object);
  const wchar_t* text = NameText(PackedName(class_object));
  if (!text) return false;
  const int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                                         -1, output,
                                         static_cast<int>(capacity), nullptr,
                                         nullptr);
  if (!result) output[0] = '\0';
  return result != 0;
}

void* UnrealObjectClass(void* object) {
  if (!UnrealObjectValid(object)) return nullptr;
  void* class_object = ClassOf(object);
  return UnrealObjectValid(class_object) &&
                 UnrealObjectIsA(class_object, L"Class")
             ? class_object
             : nullptr;
}

bool UnrealResolveExactScriptFunction(uint32_t expected_object_index,
                                      const wchar_t* full_owner_path,
                                      const wchar_t* function_name,
                                      UnrealFunctionHandle* output) {
  if (!output) return false;
  *output = {};
  if (!UnrealBridgeReady() || !full_owner_path || !*full_owner_path ||
      !function_name || !*function_name)
    return false;
  uint32_t packed_function_name = 0;
  if (!MakeName(function_name, &packed_function_name)) return false;
  const uint32_t function_name_index = packed_function_name >> 14;

  void* match = nullptr;
  if (expected_object_index) {
    void* candidate = ObjectByInternalIndex(expected_object_index);
    if (!FunctionIdentityMatches(candidate, function_name_index,
                                 full_owner_path))
      return false;
    match = candidate;
  } else {
    for (int32_t index = 0; index < g_objects->count; ++index) {
      void* candidate = g_objects->data[index];
      if (!Readable(candidate, 8) ||
          !BaseNameIndexEquals(candidate, function_name_index) ||
          !FunctionIdentityMatches(candidate, function_name_index,
                                   full_owner_path))
        continue;
      // A fully qualified owner plus function name must be unique. Refuse an
      // ambiguous result instead of relying on package load order.
      if (match && match != candidate) return false;
      match = candidate;
    }
  }
  return match && BuildExactFunctionHandle(match, output) &&
         (!expected_object_index ||
          output->function_object_index == expected_object_index);
}

bool UnrealResolveExactScriptFunctionForObject(
    void* object, const wchar_t* full_owner_path,
    const wchar_t* function_name, UnrealFunctionHandle* output) {
  if (!output) return false;
  *output = {};
  if (!UnrealBridgeReady() || !UnrealObjectValid(object) ||
      !full_owner_path || !*full_owner_path || !function_name ||
      !*function_name)
    return false;
  uint32_t packed_function_name = 0;
  if (!MakeName(function_name, &packed_function_name)) return false;
  const uint32_t function_name_index = packed_function_name >> 14;
  void* context_class = ClassOf(object);
  for (size_t depth = 0; context_class && depth < kMaxOuterDepth; ++depth) {
    if (!UnrealObjectValid(context_class) ||
        !UnrealObjectIsA(context_class, L"Class") ||
        !Readable(context_class, 0x34))
      return false;

    // Ordinary class-scoped UFunctions are direct children of the class.
    if (ObjectPathMatches(context_class, full_owner_path)) {
      void* field = *reinterpret_cast<void**>(
          static_cast<uint8_t*>(context_class) + 0x30);
      for (size_t count = 0; field && count < 4096; ++count) {
        if (!Readable(field, 0x6C)) return false;
        if (BaseNameIndexEquals(field, function_name_index) &&
            FunctionIdentityMatches(field, function_name_index,
                                    full_owner_path))
          return BuildExactFunctionHandle(field, output);
        field = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(field) + 0x2C);
      }
      return false;
    }

    // State-scoped functions are children of a UState which is itself a
    // direct child of the controller class. Walk only this bounded field tree;
    // do not fall back to a process-wide GObjects scan.
    void* state = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(context_class) + 0x30);
    for (size_t state_count = 0; state && state_count < 4096;
         ++state_count) {
      if (!Readable(state, 0x34)) return false;
      if (UnrealObjectIsA(state, L"State") &&
          ObjectPathMatches(state, full_owner_path)) {
        void* field = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(state) + 0x30);
        for (size_t count = 0; field && count < 4096; ++count) {
          if (!Readable(field, 0x6C)) return false;
          if (BaseNameIndexEquals(field, function_name_index) &&
              FunctionIdentityMatches(field, function_name_index,
                                      full_owner_path))
            return BuildExactFunctionHandle(field, output);
          field = *reinterpret_cast<void**>(
              static_cast<uint8_t*>(field) + 0x2C);
        }
        return false;
      }
      state = *reinterpret_cast<void**>(
          static_cast<uint8_t*>(state) + 0x2C);
    }
    context_class = ClassParent(context_class);
  }
  return false;
}

bool UnrealExactScriptFunctionValid(const UnrealFunctionHandle& function) {
  return UnrealBridgeReady() && ExactFunctionHandleValid(function);
}

bool UnrealCallExactVoid(void* object,
                         const UnrealFunctionHandle& function) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 0 && call.size == 0 &&
         call.return_offset == 0xFFFFu && Execute(object, &call);
}

bool UnrealCallExactVoidObject(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.return_offset == 0xFFFFu &&
         SetObjectParameter(&call, object_parameter, object_value) &&
         Execute(object, &call);
}

bool UnrealCallExactVoidInt(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* int_parameter, int32_t int_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.size == sizeof(int_value) && call.return_offset == 0xFFFFu &&
         SetIntParameter(&call, int_parameter, int_value) &&
         FieldOffset(FindCallField(call, int_parameter)) == 0 &&
         Execute(object, &call);
}

bool UnrealCallExactVoidStruct(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* struct_parameter,
    const UnrealStructValueView& struct_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.return_offset == 0xFFFFu &&
         SetStructParameter(&call, struct_parameter, struct_value) &&
         Execute(object, &call);
}

bool UnrealCallExactVoidObjectBool(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value,
    const wchar_t* bool_parameter, bool bool_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.return_offset == 0xFFFFu &&
         SetObjectParameter(&call, object_parameter, object_value) &&
         SetBoolParameter(&call, bool_parameter, bool_value) &&
         Execute(object, &call);
}

bool UnrealResolveExactObjectParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function) ||
      !parameter_name || !*parameter_name)
    return false;
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(void*))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  if (offset == UINT32_MAX || offset + sizeof(void*) > parameter_size)
    return false;
  *output = offset;
  return true;
}

bool UnrealResolveExactIntParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function) ||
      !parameter_name || !*parameter_name)
    return false;
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"IntProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(int32_t))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  const uint16_t return_offset = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x60);
  if (FunctionParameterCount(function.function) != 1 ||
      parameter_size != sizeof(int32_t) || return_offset != 0xFFFFu ||
      offset != 0)
    return false;
  *output = offset;
  return true;
}

bool UnrealResolveExactStructParameterLayout(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t expected_size, uint32_t* output_offset,
    void** output_struct_definition) {
  if (!output_offset || !output_struct_definition) return false;
  *output_offset = 0;
  *output_struct_definition = nullptr;
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function) ||
      !parameter_name || !*parameter_name || !expected_size)
    return false;
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != expected_size ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  if (!UnrealObjectValid(definition) ||
      ReflectedStructSize(definition) != expected_size ||
      offset == UINT32_MAX || offset + expected_size > parameter_size)
    return false;
  *output_offset = offset;
  *output_struct_definition = definition;
  return true;
}

bool UnrealCallExactObjectStructStructResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, void* object_value,
    const wchar_t* struct_parameter, const UnrealStructValueView& struct_value,
    void* output, uint32_t output_size) {
  if (!output || !output_size) return false;
  ReflectedCall call;
  if (!PrepareExactCall(object, function, &call) ||
      !SetObjectParameter(&call, object_parameter, object_value) ||
      !SetStructParameter(&call, struct_parameter, struct_value) ||
      !Execute(object, &call))
    return false;
  void* field = FindCallField(call, L"ReturnValue");
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x38, sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)) ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != output_size)
    return false;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(field) + 0x38);
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  if ((flags & kPropertyReturnParm) == 0 ||
      !UnrealObjectValid(definition) ||
      ReflectedStructSize(definition) != output_size ||
      offset == UINT32_MAX || offset + output_size > call.size)
    return false;
  std::memcpy(output, call.parameters.data() + offset, output_size);
  return true;
}

bool UnrealPrepareObjectStructStructCall(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* object_parameter, const wchar_t* struct_parameter,
    uint32_t struct_size, uint32_t return_size,
    UnrealPreparedObjectStructStructCall* output) {
  if (!output) return false;
  *output = {};
  if (!object_parameter || !*object_parameter || !struct_parameter ||
      !*struct_parameter || !struct_size || !return_size ||
      struct_size > UINT16_MAX || return_size > UINT16_MAX)
    return false;

  ReflectedCall reflected;
  if (!PrepareExactCall(object, function, &reflected) ||
      FunctionParameterCount(reflected.function) != 3 ||
      reflected.size == 0 || reflected.size > kMaxCallParameters)
    return false;

  void* object_field = FindCallField(reflected, object_parameter);
  void* struct_field = FindCallField(reflected, struct_parameter);
  void* return_field = FindCallField(reflected, L"ReturnValue");
  if (!object_field || !struct_field || !return_field ||
      !UnrealObjectIsA(object_field, L"ObjectProperty") ||
      !ValidInputParameter(object_field) ||
      FieldArrayDim(object_field) != 1 ||
      FieldElementSize(object_field) != sizeof(void*) ||
      !UnrealObjectIsA(struct_field, L"StructProperty") ||
      !ValidInputParameter(struct_field) ||
      FieldArrayDim(struct_field) != 1 ||
      FieldElementSize(struct_field) != struct_size ||
      !Readable(static_cast<uint8_t*>(struct_field) + 0x40,
                sizeof(void*)) ||
      !UnrealObjectIsA(return_field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x38,
                sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x40,
                sizeof(void*)) ||
      FieldArrayDim(return_field) != 1 ||
      FieldElementSize(return_field) != return_size)
    return false;

  const uint32_t return_flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(return_field) + 0x38);
  void* input_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(struct_field) + 0x40);
  void* return_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(return_field) + 0x40);
  const uint32_t object_offset = FieldOffset(object_field);
  const uint32_t struct_offset = FieldOffset(struct_field);
  const uint32_t return_offset = FieldOffset(return_field);
  if ((return_flags & kPropertyReturnParm) == 0 ||
      !UnrealObjectValid(input_definition) ||
      ReflectedStructSize(input_definition) != struct_size ||
      !UnrealObjectValid(return_definition) ||
      ReflectedStructSize(return_definition) != return_size ||
      object_offset == UINT32_MAX || struct_offset == UINT32_MAX ||
      return_offset == UINT32_MAX ||
      object_offset + sizeof(void*) > reflected.size ||
      struct_offset + struct_size > reflected.size ||
      return_offset + return_size > reflected.size ||
      reflected.return_offset != return_offset ||
      object_offset > UINT16_MAX || struct_offset > UINT16_MAX ||
      return_offset > UINT16_MAX)
    return false;

  output->function = reflected.function;
  output->parameter_size = reflected.size;
  output->object_offset = static_cast<uint16_t>(object_offset);
  output->struct_offset = static_cast<uint16_t>(struct_offset);
  output->return_offset = static_cast<uint16_t>(return_offset);
  output->struct_size = static_cast<uint16_t>(struct_size);
  output->return_size = static_cast<uint16_t>(return_size);
  output->prepared = true;
  return true;
}

bool UnrealCallPreparedObjectStructStructResult(
    void* object, const UnrealPreparedObjectStructStructCall& call,
    void* object_value, const void* struct_value, void* output) {
  if (!g_ready || !g_invoke || !object || !struct_value || !output ||
      !call.prepared || !call.function || !call.parameter_size ||
      call.parameter_size > kMaxCallParameters || !call.struct_size ||
      !call.return_size ||
      static_cast<uint32_t>(call.object_offset) + sizeof(void*) >
          call.parameter_size ||
      static_cast<uint32_t>(call.struct_offset) + call.struct_size >
          call.parameter_size ||
      static_cast<uint32_t>(call.return_offset) + call.return_size >
          call.parameter_size)
    return false;

  alignas(16) std::array<uint8_t, kMaxCallParameters> parameters;
  std::memset(parameters.data(), 0, call.parameter_size);
  std::memcpy(parameters.data() + call.object_offset, &object_value,
              sizeof(object_value));
  std::memcpy(parameters.data() + call.struct_offset, struct_value,
              call.struct_size);
  WfcInvokeReflected(g_invoke, call.function, object, parameters.data());
  std::memcpy(output, parameters.data() + call.return_offset,
              call.return_size);
  return true;
}

bool UnrealInstallFunctionExecDetour(
    const UnrealFunctionHandle& function,
    UnrealFunctionExec replacement_executor,
    UnrealFunctionExecDetour* output) {
  if (!output || output->installed || !UnrealBridgeReady() ||
      !ExactFunctionHandleValid(function) ||
      !ExecutableExecutor(replacement_executor) ||
      g_function_exec_detour_count >= g_function_exec_detours.size())
    return false;
  for (size_t index = 0; index < g_function_exec_detour_count; ++index) {
    if (g_function_exec_detours[index].function.function ==
        function.function)
      return false;
  }

  volatile LONG* slot = nullptr;
  UnrealFunctionExec original_executor = nullptr;
  if (!EnsureFunctionExecutorInitialized(function, &slot,
                                         &original_executor) ||
      original_executor == replacement_executor)
    return false;
  if (!CompareExchangeFunctionExecutor(function, original_executor,
                                       replacement_executor))
    return false;

  ActiveFunctionExecDetour active;
  active.function = function;
  active.original_executor = original_executor;
  active.replacement_executor = replacement_executor;
  g_function_exec_detours[g_function_exec_detour_count++] = active;
  output->function = function;
  output->original_executor = original_executor;
  output->replacement_executor = replacement_executor;
  output->installed = true;
  return true;
}

bool UnrealRestoreFunctionExecDetour(UnrealFunctionExecDetour* detour) {
  if (!detour || !UnrealBridgeReady()) return false;
  const int active_index = FindActiveFunctionExecDetour(*detour);
  if (active_index < 0) return false;
  const ActiveFunctionExecDetour active =
      g_function_exec_detours[static_cast<size_t>(active_index)];
  if (!CompareExchangeFunctionExecutor(active.function,
                                       active.replacement_executor,
                                       active.original_executor))
    return false;
  RemoveActiveFunctionExecDetour(static_cast<size_t>(active_index));
  *detour = {};
  return true;
}

bool UnrealRestoreAllFunctionExecDetours() {
  if (!UnrealBridgeReady()) return false;
  const auto pending = g_function_exec_detours;
  const size_t pending_count = g_function_exec_detour_count;
  g_function_exec_detours.fill({});
  g_function_exec_detour_count = 0;
  bool restored_all = true;
  for (size_t remaining = pending_count; remaining > 0; --remaining) {
    const ActiveFunctionExecDetour& active = pending[remaining - 1];
    if (CompareExchangeFunctionExecutor(active.function,
                                        active.replacement_executor,
                                        active.original_executor))
      continue;
    restored_all = false;
    if (g_function_exec_detour_count < g_function_exec_detours.size())
      g_function_exec_detours[g_function_exec_detour_count++] = active;
  }
  return restored_all;
}

bool UnrealInstallScriptObjectOperandPatch(
    const UnrealFunctionHandle& function, uint32_t opcode_offset,
    uint8_t expected_opcode, void* expected_object, void* replacement_object,
    UnrealScriptObjectOperandPatch* output) {
  if (!output || output->installed || !UnrealBridgeReady() ||
      !ExactFunctionHandleValid(function) || !expected_object ||
      !replacement_object || expected_object == replacement_object ||
      !UnrealObjectValid(expected_object) ||
      !UnrealObjectIsA(expected_object, L"Class") ||
      !UnrealObjectValid(replacement_object) ||
      !UnrealObjectIsA(replacement_object, L"Class") ||
      g_script_object_operand_patch_count >=
          g_script_object_operand_patches.size())
    return false;
  for (size_t index = 0; index < g_script_object_operand_patch_count;
       ++index) {
    const UnrealScriptObjectOperandPatch& active =
        g_script_object_operand_patches[index].patch;
    if (active.function.function == function.function &&
        active.opcode_offset == opcode_offset)
      return false;
  }
  if (!ReplaceScriptObjectOperand(function, opcode_offset, expected_opcode,
                                  expected_object, replacement_object))
    return false;

  UnrealScriptObjectOperandPatch installed;
  installed.function = function;
  installed.opcode_offset = opcode_offset;
  installed.expected_opcode = expected_opcode;
  installed.original_object = expected_object;
  installed.replacement_object = replacement_object;
  installed.installed = true;
  g_script_object_operand_patches[g_script_object_operand_patch_count++] =
      {installed};
  *output = installed;
  return true;
}

bool UnrealRestoreScriptObjectOperandPatch(
    UnrealScriptObjectOperandPatch* patch) {
  if (!patch || !UnrealBridgeReady()) return false;
  const int active_index = FindActiveScriptObjectOperandPatch(*patch);
  if (active_index < 0) return false;
  const UnrealScriptObjectOperandPatch active =
      g_script_object_operand_patches[static_cast<size_t>(active_index)].patch;
  if (!ReplaceScriptObjectOperand(
          active.function, active.opcode_offset, active.expected_opcode,
          active.replacement_object, active.original_object))
    return false;
  RemoveActiveScriptObjectOperandPatch(static_cast<size_t>(active_index));
  *patch = {};
  return true;
}

bool UnrealRestoreAllScriptObjectOperandPatches() {
  if (!UnrealBridgeReady()) return false;
  const auto pending = g_script_object_operand_patches;
  const size_t pending_count = g_script_object_operand_patch_count;
  g_script_object_operand_patches.fill({});
  g_script_object_operand_patch_count = 0;
  bool restored_all = true;
  for (size_t remaining = pending_count; remaining > 0; --remaining) {
    const UnrealScriptObjectOperandPatch& active =
        pending[remaining - 1].patch;
    if (ReplaceScriptObjectOperand(
            active.function, active.opcode_offset, active.expected_opcode,
            active.replacement_object, active.original_object))
      continue;
    restored_all = false;
    if (g_script_object_operand_patch_count <
        g_script_object_operand_patches.size())
      g_script_object_operand_patches[g_script_object_operand_patch_count++] =
          pending[remaining - 1];
  }
  return restored_all;
}

bool UnrealHasFunction(void* object, const wchar_t* function_name) {
  return FindFunction(object, function_name) != nullptr;
}

bool UnrealCanCallVoid(void* object, const wchar_t* function_name) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 0 && call.size == 0 &&
         call.return_offset == 0xFFFFu;
}

bool UnrealCanCallVoidInt(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.size == sizeof(int32_t) && call.return_offset == 0xFFFFu &&
         SetIntParameter(&call, parameter_name, 0) &&
         FieldOffset(FindCallField(call, parameter_name)) == 0;
}

bool UnrealReadObjectProperty(void* object, const wchar_t* property_name,
                              void** output) {
  if (!output) return false;
  *output = nullptr;
  void* value = nullptr;
  if (!ReadPropertyBytes(object, property_name, L"ObjectProperty", &value,
                         sizeof(value)) ||
      (value && !UnrealObjectValid(value)))
    return false;
  *output = value;
  return true;
}

bool UnrealResolveObjectPropertyOffset(void* object,
                                       const wchar_t* property_name,
                                       uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealObjectValid(object) || !property_name || !*property_name)
    return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(void*))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset < 0x28u ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(void*)))
    return false;
  *output = offset;
  return true;
}

bool UnrealResolveStructPropertyOffset(void* object,
                                       const wchar_t* property_name,
                                       uint32_t expected_size,
                                       uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealObjectValid(object) || !property_name || !*property_name ||
      !expected_size)
    return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != expected_size)
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset < 0x28u ||
      !Readable(static_cast<uint8_t*>(object) + offset, expected_size))
    return false;
  *output = offset;
  return true;
}

bool UnrealReadBoolProperty(void* object, const wchar_t* property_name,
                            bool* output) {
  if (!output || !UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t mask = *reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t value = *reinterpret_cast<uint32_t*>(
      static_cast<uint8_t*>(object) + offset);
  *output = (value & mask) != 0;
  return true;
}

bool UnrealReadIntProperty(void* object, const wchar_t* property_name,
                           int32_t* output) {
  return ReadPropertyBytes(object, property_name, L"IntProperty", output,
                           sizeof(*output));
}

bool UnrealReadByteProperty(void* object, const wchar_t* property_name,
                            uint8_t* output) {
  return ReadPropertyBytes(object, property_name, L"ByteProperty", output,
                           sizeof(*output));
}

bool UnrealReadFloatProperty(void* object, const wchar_t* property_name,
                             float* output) {
  return ReadPropertyBytes(object, property_name, L"FloatProperty", output,
                           sizeof(*output));
}

bool UnrealReadStringProperty(void* object, const wchar_t* property_name,
                               char* output, size_t capacity) {
  UnrealArrayView value;
  return ReadPropertyBytes(object, property_name, L"StrProperty", &value,
                           sizeof(value)) &&
         CopyUnrealString(value, output, capacity);
}

bool UnrealReadStringArrayElementProperty(void* object,
                                          const wchar_t* property_name,
                                          int32_t index, char* output,
                                          size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (!UnrealObjectValid(object) || index < 0) return false;
  void* array_field = FindField(object, property_name);
  if (!array_field || !UnrealObjectIsA(array_field, L"ArrayProperty") ||
      !Readable(static_cast<uint8_t*>(array_field) + 0x40, sizeof(void*)))
    return false;
  void* inner = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(array_field) + 0x40);
  if (!UnrealObjectValid(inner) || !UnrealObjectIsA(inner, L"StrProperty") ||
      FieldElementSize(inner) != sizeof(UnrealArrayView))
    return false;
  UnrealArrayView values;
  if (!ReadPropertyBytes(object, property_name, L"ArrayProperty", &values,
                         sizeof(values)) ||
      values.count < 0 || values.capacity < values.count ||
      values.count > 4096 || values.capacity > 4096 || index >= values.count ||
      !values.data ||
      !Readable(values.data, static_cast<size_t>(values.count) *
                                 sizeof(UnrealArrayView)))
    return false;
  UnrealArrayView value;
  const auto* element = static_cast<const UnrealArrayView*>(values.data) + index;
  std::memcpy(&value, element, sizeof(value));
  return CopyUnrealString(value, output, capacity);
}

bool UnrealReadArrayProperty(void* object, const wchar_t* property_name,
                             UnrealArrayView* output) {
  if (!output) return false;
  *output = {};
  UnrealArrayView value;
  if (!ReadPropertyBytes(object, property_name, L"ArrayProperty", &value,
                         sizeof(value)) ||
      value.count < 0 || value.capacity < value.count || value.count > 4096 ||
      value.capacity > 4096 ||
      (value.count && !Readable(value.data,
          static_cast<size_t>(value.count) * sizeof(void*))))
    return false;
  *output = value;
  return true;
}

bool UnrealReadFloatArrayElement(const UnrealArrayView& array, int32_t index,
                                 float* output) {
  if (!output || index < 0 || index >= array.count ||
      array.count > array.capacity || array.count > 4096 ||
      array.capacity > 4096 || !array.data)
    return false;
  const auto* element = static_cast<const float*>(array.data) + index;
  if (!Readable(element, sizeof(*element))) return false;
  std::memcpy(output, element, sizeof(*output));
  return true;
}

bool UnrealReadObjectArrayElement(const UnrealArrayView& array, int32_t index,
                                  void** output) {
  if (!output) return false;
  *output = nullptr;
  if (index < 0 || index >= array.count || array.count > array.capacity ||
      array.count > 4096 || array.capacity > 4096 || !array.data)
    return false;
  auto** objects = static_cast<void**>(array.data);
  if (!Readable(objects + index, sizeof(void*))) return false;
  void* value = objects[index];
  if (value && !UnrealObjectValid(value)) return false;
  *output = value;
  return true;
}

bool UnrealReadStructArrayProperty(void* object, const wchar_t* property_name,
                                   UnrealStructArrayView* output) {
  if (!output) return false;
  *output = {};
  if (!UnrealObjectValid(object)) return false;
  void* array_field = FindField(object, property_name);
  if (!array_field || !UnrealObjectIsA(array_field, L"ArrayProperty") ||
      !Readable(static_cast<uint8_t*>(array_field) + 0x40, sizeof(void*)))
    return false;
  void* inner = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(array_field) + 0x40);
  if (!UnrealObjectValid(inner) || !UnrealObjectIsA(inner, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(inner) + 0x40, sizeof(void*)))
    return false;
  void* struct_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(inner) + 0x40);
  if (!UnrealObjectValid(struct_definition)) return false;
  UnrealArrayView values;
  if (!ReadPropertyBytes(object, property_name, L"ArrayProperty", &values,
                         sizeof(values)) ||
      values.count < 0 || values.capacity < values.count ||
      values.count > 4096 || values.capacity > 4096)
    return false;
  const uint32_t element_size = FieldElementSize(inner);
  if (!element_size || element_size > 0x10000 ||
      (values.count && !Readable(values.data,
          static_cast<size_t>(values.count) * element_size)))
    return false;
  output->data = values.data;
  output->count = values.count;
  output->capacity = values.capacity;
  output->element_size = element_size;
  output->struct_definition = struct_definition;
  return true;
}

bool UnrealReadFixedStructArrayProperty(void* object,
                                        const wchar_t* property_name,
                                        UnrealStructArrayView* output) {
  if (!output) return false;
  *output = {};
  if (!UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* struct_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  if (!UnrealObjectValid(struct_definition)) return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t count = FieldArrayDim(field);
  const uint32_t element_size = FieldElementSize(field);
  if (offset == UINT32_MAX || !count || count > 1024 || !element_size ||
      element_size > 0x10000 ||
      !Readable(static_cast<uint8_t*>(object) + offset,
                static_cast<size_t>(count) * element_size))
    return false;
  output->data = static_cast<uint8_t*>(object) + offset;
  output->count = static_cast<int32_t>(count);
  output->capacity = static_cast<int32_t>(count);
  output->element_size = element_size;
  output->struct_definition = struct_definition;
  return true;
}

bool UnrealReadStructArrayObjectField(const UnrealStructArrayView& array,
                                      int32_t index,
                                      const wchar_t* field_name,
                                      void** output) {
  if (!output) return false;
  *output = nullptr;
  return ReadStructFieldBytes(array, index, field_name, L"ObjectProperty",
                              output, sizeof(*output)) &&
         (!*output || UnrealObjectValid(*output));
}

bool UnrealReadStructArrayStringField(const UnrealStructArrayView& array,
                                      int32_t index,
                                      const wchar_t* field_name, char* output,
                                      size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  UnrealArrayView value;
  return ReadStructFieldBytes(array, index, field_name, L"StrProperty", &value,
                              sizeof(value)) &&
         CopyUnrealString(value, output, capacity);
}

bool UnrealReadStructArrayFloatField(const UnrealStructArrayView& array,
                                     int32_t index,
                                     const wchar_t* field_name,
                                     float* output) {
  return ReadStructFieldBytes(array, index, field_name, L"FloatProperty",
                              output, sizeof(*output));
}

bool UnrealReadStructArrayIntField(const UnrealStructArrayView& array,
                                   int32_t index,
                                   const wchar_t* field_name,
                                   int32_t* output) {
  return ReadStructFieldBytes(array, index, field_name, L"IntProperty",
                              output, sizeof(*output));
}

bool UnrealReadStructFloatProperty(void* object,
                                   const wchar_t* property_name,
                                   const wchar_t* field_name, float* output) {
  if (!output || !UnrealObjectValid(object)) return false;
  void* struct_property = FindField(object, property_name);
  if (!struct_property ||
      !UnrealObjectIsA(struct_property, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(struct_property) + 0x40, sizeof(void*)))
    return false;
  void* struct_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(struct_property) + 0x40);
  void* member = FindStructField(struct_definition, field_name);
  if (!member || !UnrealObjectIsA(member, L"FloatProperty")) return false;
  const uint32_t base = FieldOffset(struct_property);
  const uint32_t offset = FieldOffset(member);
  const uint32_t struct_size = FieldElementSize(struct_property);
  if (base == UINT32_MAX || offset == UINT32_MAX ||
      offset + sizeof(*output) > struct_size ||
      !Readable(static_cast<uint8_t*>(object) + base + offset,
                sizeof(*output)))
    return false;
  std::memcpy(output, static_cast<uint8_t*>(object) + base + offset,
              sizeof(*output));
  return true;
}

bool UnrealReadStructProperty(void* object, const wchar_t* property_name,
                              UnrealStructValueView* output) {
  if (!output) return false;
  *output = {};
  if (!UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (!UnrealObjectValid(definition) || offset == UINT32_MAX || !size ||
      size > 0x10000 || ReflectedStructSize(definition) != size ||
      !Readable(static_cast<uint8_t*>(object) + offset, size))
    return false;
  output->data = static_cast<uint8_t*>(object) + offset;
  output->size = size;
  output->struct_definition = definition;
  return true;
}

bool UnrealReadStructStringField(const UnrealStructValueView& value,
                                 const wchar_t* field_name, char* output,
                                 size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (!value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition)) return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"StrProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(UnrealArrayView) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(UnrealArrayView)))
    return false;
  const auto* string_value = reinterpret_cast<const UnrealArrayView*>(
      static_cast<const uint8_t*>(value.data) + offset);
  return CopyUnrealString(*string_value, output, capacity);
}

bool UnrealReadStructStructField(const UnrealStructValueView& value,
                                 const wchar_t* field_name,
                                 UnrealStructValueView* output) {
  if (!output) return false;
  *output = {};
  if (!field_name || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  const uint32_t size = FieldElementSize(field);
  if (!UnrealObjectValid(definition) || offset == UINT32_MAX || !size ||
      offset + size > value.size || ReflectedStructSize(definition) != size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset, size))
    return false;
  output->data = static_cast<const uint8_t*>(value.data) + offset;
  output->size = size;
  output->struct_definition = definition;
  return true;
}

bool UnrealReadStructIntField(const UnrealStructValueView& value,
                              const wchar_t* field_name, int32_t* output) {
  if (!output || !field_name || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"IntProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(*output) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(*output)))
    return false;
  std::memcpy(output, static_cast<const uint8_t*>(value.data) + offset,
              sizeof(*output));
  return true;
}

bool UnrealReadStructBoolProperty(void* object,
                                  const wchar_t* property_name,
                                  const wchar_t* field_name, bool* output) {
  if (!output || !field_name) return false;
  UnrealStructValueView value;
  if (!UnrealReadStructProperty(object, property_name, &value)) return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(uint32_t) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(uint32_t)))
    return false;
  const uint32_t mask = *reinterpret_cast<const uint32_t*>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t bits = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(value.data) + offset);
  *output = (bits & mask) != 0;
  return true;
}

bool UnrealWriteObjectPropertyExpected(void* object,
                                       const wchar_t* property_name,
                                       void* expected_value,
                                       void* new_value) {
  if (!UnrealObjectValid(object) || !property_name ||
      (expected_value && !UnrealObjectValid(expected_value)) ||
      (new_value && !UnrealObjectValid(new_value)))
    return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || FieldElementSize(field) != sizeof(void*) ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(void*)))
    return false;
  auto** slot = reinterpret_cast<void**>(
      static_cast<uint8_t*>(object) + offset);
  return InterlockedCompareExchangePointer(
             reinterpret_cast<void* volatile*>(slot), new_value,
             expected_value) == expected_value;
}

bool UnrealWriteIntProperty(void* object, const wchar_t* property_name,
                            int32_t value) {
  if (!UnrealObjectValid(object) || !property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"IntProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || FieldElementSize(field) != sizeof(value) ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(value)))
    return false;
  *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(object) + offset) = value;
  return true;
}

bool UnrealWriteStructProperty(void* object, const wchar_t* property_name,
                               const UnrealStructValueView& value) {
  if (!UnrealObjectValid(object) || !property_name || !value.data ||
      !value.size || !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  const uint32_t offset = FieldOffset(field);
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  if (offset == UINT32_MAX || !definition ||
      definition != value.struct_definition ||
      FieldElementSize(field) != value.size ||
      !Readable(static_cast<uint8_t*>(object) + offset, value.size) ||
      !Readable(value.data, value.size))
    return false;
  std::memcpy(static_cast<uint8_t*>(object) + offset, value.data, value.size);
  return true;
}

bool UnrealSwapStructProperties(void* left, const wchar_t* left_property,
                                void* right,
                                const wchar_t* right_property) {
  if (!UnrealObjectValid(left) || !UnrealObjectValid(right) ||
      !left_property || !right_property)
    return false;
  UnrealStructValueView left_value;
  UnrealStructValueView right_value;
  if (!UnrealReadStructProperty(left, left_property, &left_value) ||
      !UnrealReadStructProperty(right, right_property, &right_value) ||
      left_value.struct_definition != right_value.struct_definition ||
      left_value.size != right_value.size || !left_value.size ||
      left_value.size > 4096)
    return false;
  std::array<uint8_t, 4096> temporary = {};
  std::memcpy(temporary.data(), left_value.data, left_value.size);
  std::memcpy(const_cast<void*>(left_value.data), right_value.data,
              left_value.size);
  std::memcpy(const_cast<void*>(right_value.data), temporary.data(),
              right_value.size);
  return true;
}

bool UnrealSynchronouslyAssignCharacterPair(
    void* first_pri, void* second_pri,
    const UnrealFunctionHandle& select_character,
    const UnrealFunctionHandle& client_start_character_load,
    const UnrealFunctionHandle& confirm_selected_character,
    const UnrealFunctionHandle& refresh_provider,
    const UnrealStructValueView& first_character,
    const UnrealStructValueView& second_character,
    uint32_t expected_game_thread, bool* mutation_started) {
  // These offsets and opcodes are exact-build evidence from the supported
  // TransGame.xxx SelectCharacter bytecode. Keeping the patch here, beside
  // the UFunction/script validators, prevents a generic raw byte writer from
  // escaping into gameplay code.
  constexpr int32_t kExpectedScriptSize = 0xA8;
  constexpr size_t kHeadOffset = 0x00;
  constexpr size_t kLoadResetOffset = 0x6A;
  constexpr size_t kClientCallOpcodeOffset = 0x88;
  constexpr size_t kClientCallOperandOffset = 0x89;
  constexpr size_t kReplicationIncrementOffset = 0x93;
  constexpr size_t kReturnOffset = 0xA5;
  constexpr uint8_t kOriginalHead[] = {0x07, 0x19, 0x00};
  constexpr uint8_t kPatchedHead[] = {0x06, 0x72, 0x00};
  constexpr uint8_t kLoadResetPrefix[] = {
      0x0F, 0x36, 0x01, 0x49, 0x35, 0x00, 0x00, 0x25};
  constexpr uint8_t kOriginalReplicationIncrement[] = {0xA3, 0x35, 0x52};
  constexpr uint8_t kPatchedReplicationIncrement[] = {0x06, 0xA5, 0x00};
  constexpr uint8_t kReturnSuffix[] = {0x04, 0x0B, 0x53};
  static volatile LONG patch_active = 0;

  if (mutation_started) *mutation_started = false;

  if (sizeof(void*) != 4 || !g_ready || !UnrealObjectValid(first_pri) ||
      !UnrealObjectValid(second_pri) || first_pri == second_pri ||
      !expected_game_thread || GetCurrentThreadId() != expected_game_thread ||
      !first_character.data || !second_character.data ||
      first_character.struct_definition != second_character.struct_definition ||
      first_character.size != second_character.size ||
      !ExactFunctionHandleValid(select_character) ||
      !ExactFunctionHandleValid(client_start_character_load) ||
      !ExactFunctionHandleValid(confirm_selected_character) ||
      !ExactFunctionHandleValid(refresh_provider) ||
      InterlockedCompareExchange(&patch_active, 1, 0) != 0)
    return false;

  UnrealArrayView first_load_observers = {};
  UnrealArrayView second_load_observers = {};
  UnrealStructValueView first_replication = {};
  UnrealStructValueView second_replication = {};
  int32_t first_replication_flag = 0;
  int32_t second_replication_flag = 0;
  if (!UnrealReadArrayProperty(first_pri, L"LoadCompletePlayers",
                               &first_load_observers) ||
      !UnrealReadArrayProperty(second_pri, L"LoadCompletePlayers",
                               &second_load_observers) ||
      !UnrealReadStructProperty(first_pri, L"ReplicatedCharacterData",
                                &first_replication) ||
      !UnrealReadStructProperty(second_pri, L"ReplicatedCharacterData",
                                &second_replication) ||
      !UnrealReadStructIntField(first_replication, L"ForceReplicationFlag",
                                &first_replication_flag) ||
      !UnrealReadStructIntField(second_replication, L"ForceReplicationFlag",
                                &second_replication_flag)) {
    InterlockedExchange(&patch_active, 0);
    return false;
  }

  bool valid = true;
  const auto validate_struct_parameter = [&](
      const UnrealFunctionHandle& function, const wchar_t* name) {
    ReflectedCall call;
    call.function = function.function;
    void* field = FindCallField(call, name);
    if (!field || FunctionParameterCount(function.function) != 1 ||
        !UnrealObjectIsA(field, L"StructProperty") ||
        !ValidInputParameter(field) ||
        !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
      return false;
    void* definition = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(field) + 0x40);
    return definition == first_character.struct_definition &&
           FieldElementSize(field) == first_character.size;
  };
  valid = validate_struct_parameter(select_character, L"CD") &&
          validate_struct_parameter(client_start_character_load, L"CD") &&
          validate_struct_parameter(confirm_selected_character,
                                    L"SelectedCharacter") &&
          FunctionParameterCount(refresh_provider.function) == 0;

  auto* function_bytes = static_cast<uint8_t*>(select_character.function);
  void* script = nullptr;
  int32_t script_count = 0;
  int32_t script_capacity = 0;
  if (valid &&
      Readable(function_bytes + kFunctionScriptDataOffset,
               sizeof(script) + sizeof(script_count) +
                   sizeof(script_capacity))) {
    std::memcpy(&script, function_bytes + kFunctionScriptDataOffset,
                sizeof(script));
    std::memcpy(&script_count, function_bytes + kFunctionScriptCountOffset,
                sizeof(script_count));
    std::memcpy(&script_capacity,
                function_bytes + kFunctionScriptCapacityOffset,
                sizeof(script_capacity));
  } else {
    valid = false;
  }
  auto* bytes = static_cast<uint8_t*>(script);
  void* original_client_operand = nullptr;
  if (valid) {
    valid = script_count == kExpectedScriptSize &&
            script_capacity >= script_count &&
            Readable(bytes, static_cast<size_t>(script_count)) &&
            std::memcmp(bytes + kHeadOffset, kOriginalHead,
                        sizeof(kOriginalHead)) == 0 &&
            std::memcmp(bytes + kLoadResetOffset, kLoadResetPrefix,
                        sizeof(kLoadResetPrefix)) == 0 &&
            bytes[kClientCallOpcodeOffset] == 0x1C &&
            std::memcmp(bytes + kReplicationIncrementOffset,
                        kOriginalReplicationIncrement,
                        sizeof(kOriginalReplicationIncrement)) == 0 &&
            std::memcmp(bytes + kReturnOffset, kReturnSuffix,
                        sizeof(kReturnSuffix)) == 0;
    if (valid) {
      std::memcpy(&original_client_operand,
                  bytes + kClientCallOperandOffset,
                  sizeof(original_client_operand));
      valid = original_client_operand == client_start_character_load.function;
    }
  }

  UnrealFunctionExecDetour client_detour = {};
  UnrealFunctionExecDetour refresh_detour = {};
  bool detours_installed = false;
  if (valid &&
      UnrealInstallFunctionExecDetour(
          client_start_character_load, &SuppressedIdentitySideEffect,
          &client_detour)) {
    if (UnrealInstallFunctionExecDetour(refresh_provider,
                                        &SuppressedIdentitySideEffect,
                                        &refresh_detour)) {
      detours_installed = true;
    } else {
      valid = false;
      if (!UnrealRestoreFunctionExecDetour(&client_detour))
        Log(LogLevel::kError,
            "[SquadSwitch] ClientStartCharacterLoad suppressor restoration failed; input must remain closed");
    }
  } else {
    valid = false;
  }

  DWORD previous_protection = 0;
  bool protection_changed = false;
  bool patch_installed = false;
  bool select_calls_succeeded = false;
  if (valid && detours_installed &&
      VirtualProtect(bytes, static_cast<size_t>(script_count),
                     PAGE_EXECUTE_READWRITE, &previous_protection)) {
    protection_changed = true;
    // Revalidate every exact-build guard after changing protection. The
    // ClientStartCharacterLoad operand is read with memcpy only and is never
    // rewritten: it starts at the unaligned +0x89 script offset.
    void* observed_client = nullptr;
    std::memcpy(&observed_client, bytes + kClientCallOperandOffset,
                sizeof(observed_client));
    valid = std::memcmp(bytes + kHeadOffset, kOriginalHead,
                        sizeof(kOriginalHead)) == 0 &&
            std::memcmp(bytes + kLoadResetOffset, kLoadResetPrefix,
                        sizeof(kLoadResetPrefix)) == 0 &&
            bytes[kClientCallOpcodeOffset] == 0x1C &&
            observed_client == original_client_operand &&
            std::memcmp(bytes + kReplicationIncrementOffset,
                        kOriginalReplicationIncrement,
                        sizeof(kOriginalReplicationIncrement)) == 0 &&
            std::memcmp(bytes + kReturnOffset, kReturnSuffix,
                        sizeof(kReturnSuffix)) == 0;
    if (valid) {
      // Install tail-to-head. The head jump skips observer-array reset; the
      // tail jump skips ForceReplicationFlag mutation. The intervening client
      // load call remains byte-for-byte original but its exact executor is
      // temporarily suppressed.
      std::memcpy(bytes + kReplicationIncrementOffset,
                  kPatchedReplicationIncrement,
                  sizeof(kPatchedReplicationIncrement));
      std::memcpy(bytes + kHeadOffset, kPatchedHead, sizeof(kPatchedHead));
      FlushInstructionCache(GetCurrentProcess(), bytes,
                            static_cast<size_t>(script_count));
      patch_installed = true;
      if (mutation_started) *mutation_started = true;
      select_calls_succeeded =
          UnrealCallExactVoidStruct(first_pri, select_character, L"CD",
                                    first_character) &&
          UnrealCallExactVoidStruct(second_pri, select_character, L"CD",
                                    second_character);
    }
  }

  bool restored = true;
  if (patch_installed) {
    // Restore head-to-tail. The shortened path becomes unreachable before
    // its supporting tail branch is removed.
    std::memcpy(bytes + kHeadOffset, kOriginalHead, sizeof(kOriginalHead));
    std::memcpy(bytes + kReplicationIncrementOffset,
                kOriginalReplicationIncrement,
                sizeof(kOriginalReplicationIncrement));
    FlushInstructionCache(GetCurrentProcess(), bytes,
                          static_cast<size_t>(script_count));
    restored =
        std::memcmp(bytes + kHeadOffset, kOriginalHead,
                    sizeof(kOriginalHead)) == 0 &&
        std::memcmp(bytes + kReplicationIncrementOffset,
                    kOriginalReplicationIncrement,
                    sizeof(kOriginalReplicationIncrement)) == 0;
  }
  if (protection_changed) {
    DWORD ignored = 0;
    if (!VirtualProtect(bytes, static_cast<size_t>(script_count),
                        previous_protection, &ignored))
      restored = false;
  }

  // ConfirmSelectedCharacter owns the deep assignment to _SelectedCharacter.
  // Suppress its per-PRI RefreshProvider callback until both identities and
  // both current-character fields are coherent; GameBridge publishes both
  // providers together after this helper returns.
  bool confirm_calls_succeeded = false;
  if (select_calls_succeeded && restored) {
    confirm_calls_succeeded =
        UnrealCallExactVoidStruct(first_pri, confirm_selected_character,
                                  L"SelectedCharacter", first_character) &&
        UnrealCallExactVoidStruct(second_pri, confirm_selected_character,
                                  L"SelectedCharacter", second_character);
  }

  bool detours_restored = true;
  if (refresh_detour.installed)
    detours_restored =
        UnrealRestoreFunctionExecDetour(&refresh_detour) && detours_restored;
  if (client_detour.installed)
    detours_restored =
        UnrealRestoreFunctionExecDetour(&client_detour) && detours_restored;

  UnrealArrayView first_load_after = {};
  UnrealArrayView second_load_after = {};
  UnrealStructValueView first_replication_after = {};
  UnrealStructValueView second_replication_after = {};
  int32_t first_flag_after = 0;
  int32_t second_flag_after = 0;
  const bool invariants_preserved =
      confirm_calls_succeeded &&
      UnrealReadArrayProperty(first_pri, L"LoadCompletePlayers",
                              &first_load_after) &&
      UnrealReadArrayProperty(second_pri, L"LoadCompletePlayers",
                              &second_load_after) &&
      first_load_after.data == first_load_observers.data &&
      first_load_after.count == first_load_observers.count &&
      first_load_after.capacity == first_load_observers.capacity &&
      second_load_after.data == second_load_observers.data &&
      second_load_after.count == second_load_observers.count &&
      second_load_after.capacity == second_load_observers.capacity &&
      UnrealReadStructProperty(first_pri, L"ReplicatedCharacterData",
                               &first_replication_after) &&
      UnrealReadStructProperty(second_pri, L"ReplicatedCharacterData",
                               &second_replication_after) &&
      UnrealReadStructIntField(first_replication_after,
                               L"ForceReplicationFlag", &first_flag_after) &&
      UnrealReadStructIntField(second_replication_after,
                               L"ForceReplicationFlag", &second_flag_after) &&
      first_flag_after == first_replication_flag &&
      second_flag_after == second_replication_flag;

  if (!restored)
    Log(LogLevel::kError,
        "[SquadSwitch] SelectCharacter bytecode restoration failed; input must remain closed");
  if (!detours_restored)
    Log(LogLevel::kError,
        "[SquadSwitch] temporary identity executor restoration failed; input must remain closed");
  if (confirm_calls_succeeded && !invariants_preserved)
    Log(LogLevel::kError,
        "[SquadSwitch] SelectCharacter changed asynchronous identity state; rollback cannot be proven safe");
  InterlockedExchange(&patch_active, 0);
  return valid && patch_installed && select_calls_succeeded &&
         confirm_calls_succeeded && restored && detours_restored &&
         invariants_preserved;
}

bool UnrealAssignObjectStringPropertyFromStruct(
    void* destination, const wchar_t* destination_property,
    const UnrealStructValueView& source, const wchar_t* source_field) {
  if (!g_fstring_assign || !UnrealObjectValid(destination) ||
      !destination_property || !source.data || !source.size ||
      !UnrealObjectValid(source.struct_definition) || !source_field)
    return false;
  void* destination_field = FindField(destination, destination_property);
  void* source_property = FindStructField(source.struct_definition,
                                          source_field);
  if (!destination_field || !source_property ||
      !UnrealObjectIsA(destination_field, L"StrProperty") ||
      !UnrealObjectIsA(source_property, L"StrProperty"))
    return false;
  const uint32_t destination_offset = FieldOffset(destination_field);
  const uint32_t source_offset = FieldOffset(source_property);
  if (destination_offset == UINT32_MAX || source_offset == UINT32_MAX ||
      FieldElementSize(destination_field) < sizeof(UnrealArrayView) ||
      source_offset + sizeof(UnrealArrayView) > source.size)
    return false;
  auto* destination_value = reinterpret_cast<UnrealArrayView*>(
      static_cast<uint8_t*>(destination) + destination_offset);
  const auto* source_value = reinterpret_cast<const UnrealArrayView*>(
      static_cast<const uint8_t*>(source.data) + source_offset);
  if (!Readable(destination_value, sizeof(*destination_value)) ||
      !Readable(source_value, sizeof(*source_value)) ||
      source_value->count < 0 || source_value->capacity < source_value->count ||
      source_value->count > 4096 || source_value->capacity > 4096 ||
      (source_value->count &&
       !Readable(source_value->data,
                 static_cast<size_t>(source_value->count) *
                     sizeof(wchar_t))))
    return false;
  return WfcAssignFString(g_fstring_assign, destination_value, source_value) ==
         destination_value;
}

bool UnrealAssignStructArrayStringField(
    const UnrealStructArrayView& destination, int32_t destination_index,
    const wchar_t* destination_field, const UnrealStructValueView& source,
    const wchar_t* source_field) {
  if (!g_fstring_assign || !source.data || !source.size ||
      !UnrealObjectValid(source.struct_definition)) return false;
  uint8_t* destination_element = nullptr;
  if (!StructElement(destination, destination_index, &destination_element))
    return false;
  void* destination_property = FindStructField(destination.struct_definition,
                                               destination_field);
  void* source_property = FindStructField(source.struct_definition,
                                          source_field);
  if (!destination_property || !source_property ||
      !UnrealObjectIsA(destination_property, L"StrProperty") ||
      !UnrealObjectIsA(source_property, L"StrProperty")) return false;
  const uint32_t destination_offset = FieldOffset(destination_property);
  const uint32_t source_offset = FieldOffset(source_property);
  if (destination_offset == UINT32_MAX || source_offset == UINT32_MAX ||
      destination_offset + sizeof(UnrealArrayView) > destination.element_size ||
      source_offset + sizeof(UnrealArrayView) > source.size)
    return false;
  auto* destination_value = reinterpret_cast<UnrealArrayView*>(
      destination_element + destination_offset);
  const auto* source_value = reinterpret_cast<const UnrealArrayView*>(
      static_cast<const uint8_t*>(source.data) + source_offset);
  if (!Readable(destination_value, sizeof(*destination_value)) ||
      !Readable(source_value, sizeof(*source_value)) ||
      source_value->count < 0 || source_value->capacity < source_value->count ||
      source_value->count > 4096 || source_value->capacity > 4096 ||
      (source_value->count && !Readable(source_value->data,
          static_cast<size_t>(source_value->count) * sizeof(wchar_t))))
    return false;
  if (WfcAssignFString(g_fstring_assign, destination_value, source_value) !=
      destination_value)
    return false;
  return destination_value->count == source_value->count &&
         destination_value->capacity >= destination_value->count &&
         (!destination_value->count || Readable(destination_value->data,
             static_cast<size_t>(destination_value->count) * sizeof(wchar_t)));
}

bool UnrealCallVoid(void* object, const wchar_t* function_name) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 0 && call.size == 0 &&
         call.return_offset == 0xFFFFu && Execute(object, &call);
}

bool UnrealCallBool(void* object, const wchar_t* function_name, bool* output) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) && Execute(object, &call) &&
         ReadBoolReturn(call, output);
}

bool UnrealCallInt(void* object, const wchar_t* function_name,
                   int32_t* output) {
  return CallScalar(object, function_name, output);
}

bool UnrealCallByte(void* object, const wchar_t* function_name,
                    uint8_t* output) {
  return CallScalar(object, function_name, output);
}

bool UnrealCallFloat(void* object, const wchar_t* function_name,
                     float* output) {
  return CallScalar(object, function_name, output);
}

bool UnrealCallObject(void* object, const wchar_t* function_name,
                      void** output) {
  if (!CallScalar(object, function_name, output)) return false;
  return !*output || UnrealObjectValid(*output);
}

bool UnrealCallString(void* object, const wchar_t* function_name,
                      char* output, size_t capacity) {
  UnrealArrayView value;
  if (!CallScalar(object, function_name, &value)) return false;
  return CopyUnrealString(value, output, capacity);
}

bool UnrealCallStringBool(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name, bool value,
                          char* output, size_t capacity) {
  ReflectedCall call;
  UnrealArrayView result;
  return PrepareCall(object, function_name, &call) &&
         SetBoolParameter(&call, parameter_name, value) &&
         Execute(object, &call) && ReadReturn(call, &result) &&
         CopyUnrealString(result, output, capacity);
}

bool UnrealCallObjectArray(void* object, const wchar_t* function_name,
                           UnrealArrayView* output) {
  if (!output) return false;
  *output = {};
  UnrealArrayView value;
  if (!CallScalar(object, function_name, &value) || value.count < 0 ||
      value.capacity < value.count || value.count > 4096 ||
      value.capacity > 4096 ||
      (value.count && !Readable(value.data,
          static_cast<size_t>(value.count) * sizeof(void*)))) return false;
  *output = value;
  return true;
}

bool UnrealCallVoidInt(void* object, const wchar_t* function_name,
                       const wchar_t* parameter_name, int32_t value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.size == sizeof(value) && call.return_offset == 0xFFFFu &&
         SetIntParameter(&call, parameter_name, value) &&
         FieldOffset(FindCallField(call, parameter_name)) == 0 &&
         Execute(object, &call);
}

bool UnrealCallVoidBool(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, bool value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.return_offset == 0xFFFFu &&
         SetBoolParameter(&call, parameter_name, value) &&
         FieldOffset(FindCallField(call, parameter_name)) == 0 &&
         Execute(object, &call);
}

bool UnrealCallVoidByte(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, uint8_t value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.size == sizeof(value) &&
         call.return_offset == 0xFFFFu &&
         SetByteParameter(&call, parameter_name, value) &&
         FieldOffset(FindCallField(call, parameter_name)) == 0 &&
         Execute(object, &call);
}

bool UnrealCallVoidFloat(void* object, const wchar_t* function_name,
                         const wchar_t* parameter_name, float value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.return_offset == 0xFFFFu &&
         SetFloatParameter(&call, parameter_name, value) && Execute(object, &call);
}

bool UnrealCallVoidByteBool(void* object, const wchar_t* function_name,
                            const wchar_t* byte_parameter, uint8_t byte_value,
                            const wchar_t* bool_parameter, bool bool_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.return_offset == 0xFFFFu &&
         SetByteParameter(&call, byte_parameter, byte_value) &&
         SetBoolParameter(&call, bool_parameter, bool_value) &&
         Execute(object, &call);
}

bool UnrealCallBoolInt(void* object, const wchar_t* function_name,
                       const wchar_t* parameter_name, int32_t value,
                       bool* output) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetIntParameter(&call, parameter_name, value) &&
         Execute(object, &call) && ReadBoolReturn(call, output);
}

bool UnrealCallFloatInt(void* object, const wchar_t* function_name,
                        const wchar_t* parameter_name, int32_t value,
                        float* output) {
  return CallScalarInt(object, function_name, parameter_name, value, output);
}

bool UnrealCallObjectInt(void* object, const wchar_t* function_name,
                         const wchar_t* parameter_name, int32_t value,
                         void** output) {
  if (!CallScalarInt(object, function_name, parameter_name, value, output))
    return false;
  return !*output || UnrealObjectValid(*output);
}

bool UnrealCallObjectByte(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name, uint8_t value,
                          void** output) {
  if (!output) return false;
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetByteParameter(&call, parameter_name, value) &&
         Execute(object, &call) && ReadReturn(call, output) &&
         (!*output || UnrealObjectValid(*output));
}

bool UnrealCanCallObject(void* object, const wchar_t* function_name,
                         const wchar_t* object_parameter) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetObjectParameter(&call, object_parameter, nullptr);
}

bool UnrealCallVoidObject(void* object, const wchar_t* function_name,
                          const wchar_t* object_parameter,
                          void* object_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetObjectParameter(&call, object_parameter, object_value) &&
         Execute(object, &call);
}

bool UnrealCallVoidObjectObject(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, void* first_value,
    const wchar_t* second_parameter, void* second_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.return_offset == 0xFFFFu &&
         SetObjectParameter(&call, first_parameter, first_value) &&
         SetObjectParameter(&call, second_parameter, second_value) &&
         Execute(object, &call);
}

bool UnrealCallVoidObjectObjectObject(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, void* first_value,
    const wchar_t* second_parameter, void* second_value,
    const wchar_t* third_parameter, void* third_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 3 &&
         call.return_offset == 0xFFFFu &&
         SetObjectParameter(&call, first_parameter, first_value) &&
         SetObjectParameter(&call, second_parameter, second_value) &&
         SetObjectParameter(&call, third_parameter, third_value) &&
         Execute(object, &call);
}

bool UnrealCallVoidStruct(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const UnrealStructValueView& value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetStructParameter(&call, parameter_name, value) &&
         Execute(object, &call);
}

bool UnrealCanCallObjectBool(void* object, const wchar_t* function_name,
                             const wchar_t* object_parameter,
                             const wchar_t* bool_parameter) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetObjectParameter(&call, object_parameter, nullptr) &&
         SetBoolParameter(&call, bool_parameter, false);
}

bool UnrealCallVoidObjectBool(void* object, const wchar_t* function_name,
                              const wchar_t* object_parameter,
                              void* object_value,
                              const wchar_t* bool_parameter,
                              bool bool_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetObjectParameter(&call, object_parameter, object_value) &&
         SetBoolParameter(&call, bool_parameter, bool_value) &&
         Execute(object, &call);
}

}  // namespace wfcds
