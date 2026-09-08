#include "UnrealBridge.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>

#include "Log.h"
#include "NativeScriptAbi.h"
#include "NativeReferenceCode.h"
#include "NativeObjectCloneCode.h"
#include "ReflectedCallPolicy.h"

namespace wfcds {
namespace {

constexpr uintptr_t kGObjectsRva = 0x0222F0A4u;
constexpr uintptr_t kGNamesRva = 0x0222F0F4u;
constexpr uintptr_t kClassTableRva = 0x02129FD4u;
constexpr uintptr_t kMakeFNameRva = 0x00DCC800u;
constexpr uintptr_t kInvokeRva = 0x00DE6900u;
constexpr uintptr_t kInitializeUFunctionRva = 0x00DE5E20u;
constexpr uintptr_t kFStringAssignRva = 0x00001150u;
// Called by FString::operator= at 1090116A. ECX is the allocation header;
// stack arguments are previous count, new count, element bytes; ret 0x0C.
constexpr uintptr_t kArrayResizeAllocationRva = 0x0001F100u;
constexpr size_t kFindFunctionVtableOffset = 0xC0u;
constexpr size_t kMaxCallParameters = 1024;
constexpr int32_t kMaxObjects = 500000;
constexpr int32_t kMaxNames = 262143;
// A live snapshot touches objects from many UE3 allocations. Keeping enough
// committed regions across staged reads avoids repeating VirtualQuery hundreds
// of times under Wine/Box64, where it is particularly expensive. A small
// rotating retirement budget keeps those entries from becoming permanent when
// UE3 streaming or GC releases an allocation between game-thread slices.
constexpr size_t kMemoryRegionCacheCapacity = 128;
constexpr size_t kMemoryRegionRetirePerPulse = 8;
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
// Script is an eight-byte pointer/count array in this build. +0x40 is a
// DIFFERENT pointer (the function's property array), never Script capacity.
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
using ArrayResizeAllocationFn = void(WFCDS_UNREAL_EXEC_CALL *)(
    void*, void*, int32_t, int32_t, int32_t);
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
ArrayResizeAllocationFn g_array_resize_allocation = nullptr;
using CloneObjectFn = void*(WFCDS_UNREAL_EXEC_CALL *)(void*,void*,void*,uint32_t);
CloneObjectFn g_clone_object = nullptr;
bool g_clone_allocation_verified = false;
using ObjectReferenceBarrierFn = void (__attribute__((cdecl)) *)(void*);
ObjectReferenceBarrierFn g_object_reference_barrier = nullptr;
DWORD g_reference_write_thread = 0;
bool g_ready = false;

void WFCDS_UNREAL_EXEC_CALL SuppressedIdentitySideEffect(
    void*, void*, void*, void*) {}

struct MemoryRegionCacheEntry {
  uintptr_t start = 0;
  uintptr_t end = 0;
};

struct NameCacheEntry {
  std::wstring literal;
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
size_t g_memory_region_cache_retire_cursor = 0;
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

struct OwnerCacheEntry {
  std::wstring path;
  void* owner = nullptr;
  uint32_t object_index = 0;
};
std::array<OwnerCacheEntry, 128> g_owner_cache = {};

struct ActiveFunctionExecDetour {
  UnrealFunctionHandle function = {};
  UnrealFunctionExec original_executor = nullptr;
  UnrealFunctionExec replacement_executor = nullptr;
  UnrealFunctionExec engine_original_executor = nullptr;
  UnrealFunctionExec engine_replacement_executor = nullptr;
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
  g_owner_cache.fill({});
  g_memory_region_cache.fill({});
  g_memory_region_cache_count = 0;
  g_memory_region_cache_retire_cursor = 0;
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
    if (g_memory_region_cache[index].start &&
        start >= g_memory_region_cache[index].start &&
        end <= g_memory_region_cache[index].end)
      return true;
  }
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(pointer, &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
      (info.Protect & PAGE_NOACCESS))
    return false;
  uintptr_t region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) +
                        info.RegionSize;
  if (region_end < reinterpret_cast<uintptr_t>(info.BaseAddress) ||
      region_end <= start)
    return false;
  // A valid array/metadata record can cross two readable VirtualQuery
  // regions (e.g. read-only and writable pages). Validate every covered
  // region instead of treating the first region boundary as an invalid
  // pointer. Never merge across a guard, reservation or inaccessible page.
  while (end > region_end) {
    MEMORY_BASIC_INFORMATION next = {};
    if (!VirtualQuery(reinterpret_cast<const void*>(region_end), &next, sizeof(next)) ||
        next.State != MEM_COMMIT || (next.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
      return false;
    const uintptr_t next_end = reinterpret_cast<uintptr_t>(next.BaseAddress) + next.RegionSize;
    if (next_end <= region_end) return false;
    region_end = next_end;
  }
  MemoryRegionCacheEntry entry = {
      reinterpret_cast<uintptr_t>(info.BaseAddress), region_end};
  for (size_t index = 0; index < g_memory_region_cache_count; ++index) {
    if (!g_memory_region_cache[index].start) {
      g_memory_region_cache[index] = entry;
      return true;
    }
  }
  if (g_memory_region_cache_count < g_memory_region_cache.size()) {
    g_memory_region_cache[g_memory_region_cache_count++] = entry;
  } else {
    // Once the fixed cache fills, keep newly streamed allocations cacheable.
    // The prior implementation permanently favored the first 128 regions and
    // paid a VirtualQuery thunk on every later access.
    g_memory_region_cache[g_memory_region_cache_retire_cursor] = entry;
    g_memory_region_cache_retire_cursor =
        (g_memory_region_cache_retire_cursor + 1) %
        g_memory_region_cache.size();
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

bool NameEqualsText(void* object, const wchar_t* expected) {
  if (!object || !expected || !*expected) return false;
  return NameEqualsPathSegment(object, expected,
                               expected + std::wcslen(expected));
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
    // Cooked WFC packages can retain an internal linker/package outer above
    // the public Unreal reference path (for example
    // `TransGame.TnGameLobbyGame`).  The public path is still an exact suffix
    // of the runtime chain. Requiring the final matched package to have a null
    // Outer rejected every otherwise exact UFunction in the live build.
    if (segment_begin == full_path) return true;
    if (segment_begin == full_path + 1 || segment_begin[-1] != L'.')
      return false;
    segment_end = segment_begin - 1;
  }
  return false;
}

bool OwnerPathMatches(void* object, const wchar_t* full_path) {
  if (ObjectPathMatches(object, full_path)) return true;
  if (!UnrealObjectValid(object) || !full_path || !*full_path) return false;
  const wchar_t* leaf = std::wcsrchr(full_path, L'.');
  leaf = leaf ? leaf + 1 : full_path;
  const wchar_t* end = full_path + std::wcslen(full_path);
  // Keep this fallback exact at the declaring class/state leaf. Resolution by
  // function name still rejects ambiguity process-wide, while tolerating a
  // cooked package outer that is not represented in UObject's packed Outer
  // identity.
  return leaf < end && NameEqualsPathSegment(object, leaf, end);
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
    if (entry.literal.empty()) break;
    if (entry.literal == text) {
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
    if (entry.literal.empty()) {
      entry = {text, *output};
      ++g_name_cache_count;
      break;
    }
  }
  return true;
}

// Resolve metadata through the registered class table and its bounded field
// tree, not a twice-repeated scan of every actor/property in GObjects. Missing
// functions used to cost ~250ms each under Box64, including on ordinary menu
// polling. Cache only successful owner identities; a later package load may
// supply a previously absent class/state.
void* FindMetadataOwner(const wchar_t* path) {
  if (!path || !*path || std::wcslen(path) > 512 ||
      !Readable(g_class_table, 4096 * sizeof(void*)))
    return nullptr;
  for (auto& entry : g_owner_cache) {
    if (entry.path != path) continue;
    if (UnrealObjectValid(entry.owner) &&
        UnrealObjectIndex(entry.owner) == entry.object_index &&
        OwnerPathMatches(entry.owner, path)) return entry.owner;
    entry = {};
  }
  const wchar_t* leaf = std::wcsrchr(path, L'.');
  leaf = leaf ? leaf + 1 : path;
  uint32_t packed = 0;
  if (!MakeName(leaf, &packed)) return nullptr;
  void* match = nullptr;
  for (size_t i = 1; i < 4096; ++i) {
    void* candidate = g_class_table[i];
    if (!candidate || !BaseNameIndexEquals(candidate, packed >> 14) ||
        !UnrealObjectValid(candidate) ||
        !UnrealObjectIsA(candidate, L"Class") ||
        !OwnerPathMatches(candidate, path)) continue;
    if (match && match != candidate) return nullptr;
    match = candidate;
  }
  // A state is a direct child of its declaring class, not an entry in the
  // global class table. Resolve the complete parent path before walking it.
  if (!match && leaf != path) {
    const std::wstring parent_path(path, leaf - 1);
    void* parent = FindMetadataOwner(parent_path.c_str());
    if (parent && Readable(parent, 0x34)) {
      void* field = *reinterpret_cast<void**>(static_cast<uint8_t*>(parent) + 0x30);
      for (size_t n = 0; field && n < 4096; ++n) {
        if (!Readable(field, 0x34)) break;
        if (BaseNameIndexEquals(field, packed >> 14) &&
            UnrealObjectIsA(field, L"State") && ObjectOuter(field) == parent &&
            OwnerPathMatches(field, path)) {
          if (match && match != field) return nullptr;
          match = field;
        }
        field = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x2C);
      }
    }
  }
  if (match) {
    for (auto& entry : g_owner_cache) {
      if (!entry.owner) {
        entry = {path, match, UnrealObjectIndex(match)};
        break;
      }
    }
  }
  return match;
}

void DiagnoseFunctionOwner(void* owner, uint32_t wanted_name) {
  static std::array<void*, 12> diagnosed = {};
  if (!owner || std::find(diagnosed.begin(), diagnosed.end(), owner) != diagnosed.end())
    return;
  auto slot = std::find(diagnosed.begin(), diagnosed.end(), nullptr);
  if (slot == diagnosed.end()) return;
  *slot = owner;
  for (unsigned depth = 0; owner && depth < 3; ++depth, owner = ClassParent(owner)) {
    if (!UnrealObjectValid(owner) || !Readable(owner, 0x64)) break;
    auto* map = reinterpret_cast<UnrealArray*>(static_cast<uint8_t*>(owner) + 0x58);
    Log(LogLevel::kInfo,
        "[Reflection] owner metadata object=%lu name=%ls functionMap=%p count=%ld capacity=%ld children=%p",
        static_cast<unsigned long>(UnrealObjectIndex(owner)), NameText(PackedName(owner)),
        map->data, static_cast<long>(map->count), static_cast<long>(map->capacity),
        *reinterpret_cast<void**>(static_cast<uint8_t*>(owner) + 0x30));
    // UState/UClass's sorted function table is key/value pairs, not a UObject
    // pointer array (verified in 116c930a and 10ce4280). Sample only a handful
    // of entries, once per receiver type, with no script invocation.
    if (map->count <= 0 || map->count > 4096 || map->capacity < map->count ||
        map->capacity > 8192 || !Readable(map->data, map->count * 8u)) continue;
    for (int32_t i = 0; i < map->count; ++i) {
      const auto* pair = reinterpret_cast<const uint32_t*>(map->data) + i * 2;
      if (i >= 4 && pair[0] != wanted_name) continue;
      void* function = reinterpret_cast<void*>(pair[1]);
      const wchar_t* key = NameText(pair[0]);
      const wchar_t* stored = NameText(PackedName(function));
      Log(LogLevel::kInfo,
          "[Reflection] function entry key=%08lX text=%ls value=%p storedName=%ls valid=%u",
          static_cast<unsigned long>(pair[0]), key ? key : L"<unreadable>",
          function, stored ? stored : L"<unreadable>", UnrealObjectValid(function) ? 1u : 0u);
    }
  }
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
      (!BaseNameIndexEquals(function, packed >> 14) &&
       !NameEqualsText(function, name))) {
    // Preserve the engine's raw answer in the diagnostic. Previously null
    // meant either "engine found nothing" OR "our validation rejected it",
    // concealing the shared failure behind dozens of unrelated call errors.
    static size_t diagnostics = 0;
    if (diagnostics < 8) {
      ++diagnostics;
      Log(LogLevel::kWarning,
          "[Reflection] raw lookup name=%ls packed=%08lX roundtrip=%ls receiver=%ls resolver=%p raw=%p valid=%u rawName=%ls rawClass=%ls",
          name, static_cast<unsigned long>(packed),
          NameText(packed) ? NameText(packed) : L"<unreadable>",
          NameText(PackedName(object_class)), address, function,
          UnrealObjectValid(function) ? 1u : 0u,
          NameText(PackedName(function)) ? NameText(PackedName(function)) : L"<unreadable>",
          NameText(PackedName(ClassOf(function))) ? NameText(PackedName(ClassOf(function))) : L"<unreadable>");
    }
    DiagnoseFunctionOwner(object_class, packed);
    function = nullptr;
  }
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

bool ValidCallableFunction(void* function) {
  if (!function || !Readable(function, 0x6C) ||
      !UnrealObjectValid(function) || !UnrealObjectIsA(function, L"Function"))
    return false;
  const auto* bytes = static_cast<const uint8_t*>(function);
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(bytes + 0x54);
  const uint16_t token = *reinterpret_cast<const uint16_t*>(bytes + 0x58);
  // Keep native admission restricted to exact declaring classes/signatures.
  // Collision uses the real component executor, not the stripped TransGame
  // wrapper or a raw flag write that leaves the PhysX shapes unchanged.
  // g_invoke builds FFrame and runs the native executor (116e692a..116e6a86).
  const bool native_allowed = (flags == 0x02020400u &&
      NameEqualsText(function, L"SetViewTarget") &&
      OwnerPathMatches(ObjectOuter(function), L"Engine.PlayerController")) ||
      (flags == 0x00020400u && NameEqualsText(function,L"SetRBCollidesWithChannel") &&
       UnrealObjectIsA(ObjectOuter(function),L"Class") &&
       NameEqualsText(ObjectOuter(function),L"PrimitiveComponent") &&
       (ObjectPathMatches(ObjectOuter(function),L"Engine.PrimitiveComponent") ||
        !ObjectOuter(ObjectOuter(function))));
  const auto kind = ClassifyReflectedCall(flags, token, native_allowed);
  if (kind == ReflectedCallKind::kRejected) return false;
  if (kind == ReflectedCallKind::kEmpty) return true;
  // Shipping WFC strips non-exported compiled-script UFunctions. A remaining
  // Defined flag is not proof of an executable call: 116e6a74 skips a null
  // executor, while the previous bridge still reported success to callers.
  void* executor=*reinterpret_cast<void* const*>(bytes + kFunctionExecOffset);
  return executor && (kind!=ReflectedCallKind::kNative || Executable(executor));
}

bool ValidScriptDelegate(const UnrealScriptDelegate& value) {
  if (!value.object)
    return !value.function_name && !value.compiled_context && !value.compiled_function;
  if (!UnrealObjectValid(value.object)) return false;
  // The native context can be a secondary base. Retain it verbatim; do not
  // reconstruct it from the UObject or discard the paired compiled target.
  if (value.compiled_context && !value.compiled_function) return false;
  return !value.compiled_function || Executable(value.compiled_function);
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
                             const wchar_t* function_name,
                             const wchar_t* full_owner_path) {
  if (!ValidScriptFunction(function) ||
      (!BaseNameIndexEquals(function, function_name_index) &&
       !NameEqualsText(function, function_name)))
    return false;
  void* owner = ObjectOuter(function);
  return owner && UnrealObjectIsA(owner, L"Struct") &&
         FunctionContextClass(owner) &&
         OwnerPathMatches(owner, full_owner_path);
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
  return ValidCallableFunction(function)
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
  if (!ValidCallableFunction(call->function)) {
    char object_class[96] = {};
    UnrealObjectClassName(object, object_class, sizeof(object_class));
    const bool metadata_readable = Readable(call->function, 0x6C);
    LogRateLimited("reflected-call-rejected", 1000, LogLevel::kWarning,
        "[Reflection] call rejected class=%s function=%ls resolved=%p flags=%08lX token=%u",
        object_class, function_name, call->function,
        metadata_readable ? static_cast<unsigned long>(*reinterpret_cast<
            const uint32_t*>(static_cast<const uint8_t*>(call->function) + 0x54)) : 0ul,
        metadata_readable ? static_cast<unsigned>(*reinterpret_cast<
            const uint16_t*>(static_cast<const uint8_t*>(call->function) + 0x58)) : 0u);
    return false;
  }
  call->size = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x5E);
  call->return_offset = *reinterpret_cast<uint16_t*>(
      static_cast<uint8_t*>(call->function) + 0x60);
  return call->size <= call->parameters.size() &&
         (call->return_offset == 0xFFFFu || call->return_offset < call->size);
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

bool SetStringParameter(ReflectedCall* call, const wchar_t* parameter_name,
                        const wchar_t* value) {
  if (!call || !call->function || !parameter_name || !value) return false;
  void* field = FindCallField(*call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"StrProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(UnrealArrayView))
    return false;
  const size_t length = std::wcslen(value);
  if (length > 4095) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX ||
      offset + sizeof(UnrealArrayView) > call->size)
    return false;
  UnrealArrayView string_value = {
      const_cast<wchar_t*>(value), static_cast<int32_t>(length + 1),
      static_cast<int32_t>(length + 1)};
  std::memcpy(call->parameters.data() + offset, &string_value,
              sizeof(string_value));
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
  if (!call || !ValidCallableFunction(call->function) ||
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

struct ScriptExecutorAdapters {
  UnrealFunctionExec original = nullptr;
  UnrealFunctionExec replacement = nullptr;
  UnrealFunctionExec normalized_original = nullptr;
  UnrealFunctionExec engine_replacement = nullptr;
};

// Cached RX tail thunks remain allocated for the pinned DLL's process lifetime.
// Restore never frees code that a nested original callback could still reach.
// Reinstalling the same pair reuses it; no allocation occurs in an executor.
std::array<ScriptExecutorAdapters, kMaxFunctionExecDetours * 2> g_script_adapters;

bool AdaptCompiledScriptExecutors(UnrealFunctionExec original,
                                  UnrealFunctionExec replacement,
                                  ScriptExecutorAdapters* output) {
  if (!output) return false;
  ScriptExecutorAdapters* available = nullptr;
  for (auto& entry : g_script_adapters) {
    if (entry.original == original && entry.replacement == replacement) {
      *output = entry;
      return true;
    }
    if (!entry.original && !available) available = &entry;
  }
  if (!available) return false;
  auto* code = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (!code) return false;
  WriteScriptContextThunk(code, 16, static_cast<uint32_t>(ExecutorBits(original)));
  WriteScriptContextThunk(code + 16, -16, static_cast<uint32_t>(ExecutorBits(replacement)));
  DWORD previous = 0;
  if (!VirtualProtect(code, 32, PAGE_EXECUTE_READ, &previous)) {
    VirtualFree(code, 0, MEM_RELEASE);
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), code, 32);
  *available = {original, replacement,
      reinterpret_cast<UnrealFunctionExec>(code),
      reinterpret_cast<UnrealFunctionExec>(code + 16)};
  *output = *available;
  return true;
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
        active.replacement_executor == detour.replacement_executor &&
        active.engine_original_executor == detour.engine_original_executor &&
        active.engine_replacement_executor == detour.engine_replacement_executor)
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
                sizeof(void*) + sizeof(int32_t)))
    return nullptr;
  auto* base = static_cast<uint8_t*>(function.function);
  // A compiled method ignores the cooked bytecode. Never label a bytecode
  // patch of that method as a successful runtime role adapter.
  if (*reinterpret_cast<const uint32_t*>(base + 0x54) & kFunctionNativeScript)
    return nullptr;
  void* script = *reinterpret_cast<void**>(base + kFunctionScriptDataOffset);
  const int32_t count = *reinterpret_cast<const int32_t*>(
      base + kFunctionScriptCountOffset);
  constexpr size_t kOperandBytes = 1 + sizeof(void*);
  if (!script || count <= 0 || count > kMaxScriptBytes ||
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
    if (BaseNameIndexEquals(field, name_index) ||
        NameEqualsText(field, field_name)) {
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
  const auto barrier_code = native_reference_code::Relocate(
      native_reference_code::kBarrier, reinterpret_cast<uintptr_t>(executable), {15, 38});
  auto* barrier = base + (native_reference_code::kBarrierVa - native_reference_code::kImageBase);
  g_object_reference_barrier = ExactModuleCodeMatches(executable, barrier,
      barrier_code.data(), barrier_code.size())
      ? reinterpret_cast<ObjectReferenceBarrierFn>(barrier) : nullptr;
  if (!g_object_reference_barrier)
    Log(LogLevel::kError,
        "[SquadSwitch] native reference barrier signature mismatch; reference writes disabled");
  g_reference_write_thread = 0;  // Initialization may be on the loader worker.
  constexpr uint8_t resize_prologue[] = {
      0x56, 0x8B, 0x74, 0x24, 0x0C, 0x57, 0x8B, 0xF9,
      0x85, 0xF6, 0x0F, 0x84, 0x9E, 0x00, 0x00, 0x00};
  constexpr uint8_t resize_return[] = {0x5F, 0x5E, 0xC2, 0x0C, 0x00};
  const auto* resize = base + kArrayResizeAllocationRva;
  g_array_resize_allocation =
      Readable(resize, 0xDC) &&
              std::memcmp(resize, resize_prologue, sizeof(resize_prologue)) == 0 &&
              std::memcmp(resize + 0xD7, resize_return, sizeof(resize_return)) == 0
          ? reinterpret_cast<ArrayResizeAllocationFn>(
                const_cast<uint8_t*>(resize)) : nullptr;
  if (!g_array_resize_allocation)
    Log(LogLevel::kError,
        "[CampaignClass] native array allocator signature mismatch; loadout edits unavailable");
  const auto clone_code=native_clone_code::Clone(reinterpret_cast<uintptr_t>(executable));
  const auto allocation_code=native_clone_code::ArrayAllocation(reinterpret_cast<uintptr_t>(executable));
  auto* clone=base+(native_clone_code::kCloneVa-native_reference_code::kImageBase);
  g_clone_object=ExactModuleCodeMatches(executable,clone,clone_code.data(),clone_code.size())
      ? reinterpret_cast<CloneObjectFn>(clone):nullptr;
  g_clone_allocation_verified=ExactModuleCodeMatches(executable,resize,allocation_code.data(),allocation_code.size());
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
  g_array_resize_allocation = nullptr;
  g_clone_object = nullptr;
  g_clone_allocation_verified = false;
  g_object_reference_barrier = nullptr;
  g_reference_write_thread = 0;
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
  if (!g_reference_write_thread) g_reference_write_thread = GetCurrentThreadId();
  // Full per-slice invalidation measured 9-19 ms under Wine/Box64, while never
  // invalidating allowed a decommitted UE3 allocation to remain trusted until
  // the entire world changed. Retire only a bounded portion on each due pulse.
  // Reused entries are revalidated lazily by Readable, spreading VirtualQuery
  // work across slices and bounding the lifetime of stale region knowledge.
  if (!g_memory_region_cache_count) return;
  const size_t retire_count = std::min(kMemoryRegionRetirePerPulse,
                                       g_memory_region_cache_count);
  for (size_t retired = 0; retired < retire_count; ++retired) {
    g_memory_region_cache[g_memory_region_cache_retire_cursor] = {};
    g_memory_region_cache_retire_cursor =
        (g_memory_region_cache_retire_cursor + 1) %
        g_memory_region_cache_count;
  }
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

void* UnrealObjectOuter(void* object) {
  if (!UnrealObjectValid(object)) return nullptr;
  void* outer=ObjectOuter(object);
  uint32_t identity=0;
  std::memcpy(&identity,static_cast<const uint8_t*>(object)+0x14,4);
  const uint32_t expected_index=(identity>>12)&0x3FFFFu;
  return outer!=object && UnrealObjectValid(outer) && UnrealObjectIndex(outer)==expected_index ? outer:nullptr;
}

bool UnrealUnnumberedNameEquals(uint32_t packed_name,const wchar_t* expected) {
  if (!UnrealBridgeReady() || (packed_name&0x3FFFu) || !expected || !*expected) return false;
  const wchar_t* actual=NameText(packed_name);
  return actual && CompareStringOrdinal(actual,-1,expected,-1,TRUE)==CSTR_EQUAL;
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
  uint32_t name_candidates = 0;
  uint32_t text_name_candidates = 0;
  uint32_t script_candidates = 0;
  uint32_t owner_candidates = 0;
  if (expected_object_index) {
    void* candidate = ObjectByInternalIndex(expected_object_index);
    if (!FunctionIdentityMatches(candidate, function_name_index,
                                 function_name,
                                 full_owner_path))
      return false;
    match = candidate;
  } else {
    void* owner = FindMetadataOwner(full_owner_path);
    void* candidate = owner && Readable(owner, 0x34)
        ? *reinterpret_cast<void**>(static_cast<uint8_t*>(owner) + 0x30)
        : nullptr;
    for (size_t n = 0; candidate && n < 4096; ++n) {
      if (!Readable(candidate, 0x34)) return false;
      void* next = *reinterpret_cast<void**>(static_cast<uint8_t*>(candidate) + 0x2C);
      const bool index_match = BaseNameIndexEquals(candidate, function_name_index);
      if (index_match || NameEqualsText(candidate, function_name)) {
        if (index_match) ++name_candidates; else ++text_name_candidates;
        if (ValidScriptFunction(candidate)) ++script_candidates;
        if (ObjectOuter(candidate) == owner &&
            FunctionIdentityMatches(candidate, function_name_index,
                                    function_name, full_owner_path)) {
          ++owner_candidates;
          if (match && match != candidate) return false;
          match = candidate;
        }
      }
      candidate = next;
    }
  }
  const bool resolved =
      match && BuildExactFunctionHandle(match, output) &&
      (!expected_object_index ||
       output->function_object_index == expected_object_index);
  if (!resolved) {
    DiagnoseFunctionOwner(FindMetadataOwner(full_owner_path), packed_function_name);
    Log(LogLevel::kError,
        "[Reflection] exact function resolution failed owner=%ls function=%ls expected-object=%lu candidates[name=%lu text=%lu script=%lu owner=%lu]",
        full_owner_path, function_name,
        static_cast<unsigned long>(expected_object_index),
        static_cast<unsigned long>(name_candidates),
        static_cast<unsigned long>(text_name_candidates),
        static_cast<unsigned long>(script_candidates),
        static_cast<unsigned long>(owner_candidates));
  }
  return resolved;
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
  // Ask WFC's own class resolver first. This is the same authoritative lookup
  // used by normal reflected calls and reaches inherited cooked functions even
  // when their export FName uses a different packed comparison index.
  void* resolved = FindFunction(object, function_name);
  if (resolved &&
      FunctionIdentityMatches(resolved, function_name_index, function_name,
                              full_owner_path) &&
      BuildExactFunctionHandle(resolved, output))
    return true;
  void* context_class = ClassOf(object);
  for (size_t depth = 0; context_class && depth < kMaxOuterDepth; ++depth) {
    if (!UnrealObjectValid(context_class) ||
        !UnrealObjectIsA(context_class, L"Class") ||
        !Readable(context_class, 0x34))
      return false;

    // Ordinary class-scoped UFunctions are direct children of the class.
    if (OwnerPathMatches(context_class, full_owner_path)) {
      void* field = *reinterpret_cast<void**>(
          static_cast<uint8_t*>(context_class) + 0x30);
      for (size_t count = 0; field && count < 4096; ++count) {
        if (!Readable(field, 0x6C)) return false;
        if ((BaseNameIndexEquals(field, function_name_index) ||
             NameEqualsText(field, function_name)) &&
            FunctionIdentityMatches(field, function_name_index,
                                    function_name,
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
          OwnerPathMatches(state, full_owner_path)) {
        void* field = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(state) + 0x30);
        for (size_t count = 0; field && count < 4096; ++count) {
          if (!Readable(field, 0x6C)) return false;
          if ((BaseNameIndexEquals(field, function_name_index) ||
               NameEqualsText(field, function_name)) &&
              FunctionIdentityMatches(field, function_name_index,
                                      function_name,
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

bool UnrealCallExactVoidString(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 1 &&
         call.size == sizeof(UnrealArrayView) &&
         call.return_offset == 0xFFFFu &&
         SetStringParameter(&call, string_parameter, string_value) &&
         FieldOffset(FindCallField(call, string_parameter)) == 0 &&
         Execute(object, &call);
}

bool UnrealCallExactVoidStringInt(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    const wchar_t* int_parameter, int32_t int_value) {
  ReflectedCall call;
  return PrepareExactCall(object, function, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.size == sizeof(UnrealArrayView) + sizeof(int32_t) &&
         call.return_offset == 0xFFFFu &&
         SetStringParameter(&call, string_parameter, string_value) &&
         SetIntParameter(&call, int_parameter, int_value) &&
         Execute(object, &call);
}

bool UnrealCallExactVoidIntDelegate(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* int_parameter, int32_t int_value,
    const wchar_t* delegate_parameter, const UnrealScriptDelegate& callback) {
  ReflectedCall call;
  uint32_t delegate_offset = 0;
  if (!PrepareExactCall(object, function, &call) ||
      !UnrealValidateExactIntDelegateCall(function, int_parameter, delegate_parameter) ||
      !SetIntParameter(&call, int_parameter, int_value) ||
      !UnrealResolveExactDelegateParameterOffset(function, delegate_parameter,
                                                &delegate_offset) ||
      !ValidScriptDelegate(callback))
    return false;
  std::memcpy(call.parameters.data() + delegate_offset, &callback, sizeof(callback));
  return Execute(object, &call);
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

bool UnrealCallExactStructResultPersistent(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* struct_parameter,
    const UnrealStructValueView& struct_value, void* parameter_storage,
    size_t parameter_storage_size, UnrealStructValueView* output) {
  if (!g_ready || !g_invoke || !parameter_storage ||
      !parameter_storage_size || !output)
    return false;
  *output = {};
  ReflectedCall call;
  if (!PrepareExactCall(object, function, &call) || !struct_parameter ||
      !struct_value.data || !struct_value.size ||
      !UnrealObjectValid(struct_value.struct_definition) ||
      call.size > parameter_storage_size || !call.size)
    return false;
  void* input_field = FindCallField(call, struct_parameter);
  void* return_field = FindCallField(call, L"ReturnValue");
  if (!input_field || !return_field ||
      !UnrealObjectIsA(input_field, L"StructProperty") ||
      !ValidInputParameter(input_field) ||
      !UnrealObjectIsA(return_field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(input_field) + 0x40, sizeof(void*)) ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x38,
                sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x40, sizeof(void*)))
    return false;
  void* input_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(input_field) + 0x40);
  void* return_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(return_field) + 0x40);
  const uint32_t return_flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(return_field) + 0x38);
  const uint32_t input_offset = FieldOffset(input_field);
  const uint32_t return_offset = FieldOffset(return_field);
  const uint32_t return_size = FieldElementSize(return_field);
  if (FunctionParameterCount(call.function) != 2 ||
      input_definition != struct_value.struct_definition ||
      FieldElementSize(input_field) != struct_value.size ||
      ReflectedStructSize(input_definition) != struct_value.size ||
      (return_flags & kPropertyReturnParm) == 0 ||
      !UnrealObjectValid(return_definition) || !return_size ||
      return_size > 0x10000 ||
      ReflectedStructSize(return_definition) != return_size ||
      input_offset == UINT32_MAX || return_offset == UINT32_MAX ||
      input_offset + struct_value.size > call.size ||
      return_offset + return_size > call.size ||
      call.return_offset != return_offset ||
      !Readable(struct_value.data, struct_value.size))
    return false;
  std::memset(parameter_storage, 0, call.size);
  std::memcpy(static_cast<uint8_t*>(parameter_storage) + input_offset,
              struct_value.data, struct_value.size);
  WfcInvokeReflected(g_invoke, call.function, object, parameter_storage);
  output->data = static_cast<uint8_t*>(parameter_storage) + return_offset;
  output->size = return_size;
  output->struct_definition = return_definition;
  return true;
}

bool UnrealCallExactStringStructResultPersistent(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    void* parameter_storage, size_t parameter_storage_size,
    UnrealStructValueView* output) {
  if (!g_ready || !g_invoke || !string_parameter || !string_value ||
      !parameter_storage || !parameter_storage_size || !output)
    return false;
  *output = {};
  ReflectedCall call;
  if (!PrepareExactCall(object, function, &call) || !call.size ||
      call.size > parameter_storage_size)
    return false;
  void* input_field = FindCallField(call, string_parameter);
  void* return_field = FindCallField(call, L"ReturnValue");
  if (!input_field || !return_field ||
      !UnrealObjectIsA(input_field, L"StrProperty") ||
      !ValidInputParameter(input_field) ||
      FieldElementSize(input_field) != sizeof(UnrealArrayView) ||
      !UnrealObjectIsA(return_field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x38,
                sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x40, sizeof(void*)))
    return false;
  const uint32_t return_flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(return_field) + 0x38);
  void* return_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(return_field) + 0x40);
  const uint32_t input_offset = FieldOffset(input_field);
  const uint32_t return_offset = FieldOffset(return_field);
  const uint32_t return_size = FieldElementSize(return_field);
  const size_t string_length = std::wcslen(string_value);
  if (FunctionParameterCount(call.function) != 2 || string_length > 4095 ||
      (return_flags & kPropertyReturnParm) == 0 ||
      !UnrealObjectValid(return_definition) || !return_size ||
      return_size > 0x10000 ||
      ReflectedStructSize(return_definition) != return_size ||
      input_offset == UINT32_MAX || return_offset == UINT32_MAX ||
      input_offset + sizeof(UnrealArrayView) > call.size ||
      return_offset + return_size > call.size ||
      call.return_offset != return_offset)
    return false;
  std::memset(parameter_storage, 0, call.size);
  const UnrealArrayView input = {
      const_cast<wchar_t*>(string_value),
      static_cast<int32_t>(string_length + 1),
      static_cast<int32_t>(string_length + 1)};
  std::memcpy(static_cast<uint8_t*>(parameter_storage) + input_offset,
              &input, sizeof(input));
  WfcInvokeReflected(g_invoke, call.function, object, parameter_storage);
  output->data = static_cast<uint8_t*>(parameter_storage) + return_offset;
  output->size = return_size;
  output->struct_definition = return_definition;
  return true;
}

bool UnrealCallExactStringObjectResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    void** output) {
  if (!output) return false;
  *output = nullptr;
  ReflectedCall call;
  if (!PrepareExactCall(object, function, &call) ||
      FunctionParameterCount(call.function) != 2 ||
      !SetStringParameter(&call, string_parameter, string_value))
    return false;
  void* input_field = FindCallField(call, string_parameter);
  void* return_field = FindCallField(call, L"ReturnValue");
  if (!input_field || !return_field ||
      !UnrealObjectIsA(input_field, L"StrProperty") ||
      !ValidInputParameter(input_field) ||
      FieldArrayDim(input_field) != 1 ||
      FieldElementSize(input_field) != sizeof(UnrealArrayView) ||
      !UnrealObjectIsA(return_field, L"ObjectProperty") ||
      FieldArrayDim(return_field) != 1 ||
      FieldElementSize(return_field) != sizeof(void*) ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x38,
                sizeof(uint32_t)))
    return false;
  const uint32_t return_flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(return_field) + 0x38);
  const uint32_t input_offset = FieldOffset(input_field);
  const uint32_t return_offset = FieldOffset(return_field);
  if ((return_flags & kPropertyReturnParm) == 0 ||
      input_offset == UINT32_MAX || return_offset == UINT32_MAX ||
      input_offset + sizeof(UnrealArrayView) > call.size ||
      return_offset + sizeof(void*) > call.size ||
      call.return_offset != return_offset || !Execute(object, &call))
    return false;
  void* returned = nullptr;
  std::memcpy(&returned, call.parameters.data() + return_offset,
              sizeof(returned));
  if (!returned || !UnrealObjectValid(returned)) return false;
  *output = returned;
  return true;
}

bool UnrealCallExactStringBoolResult(
    void* object, const UnrealFunctionHandle& function,
    const wchar_t* string_parameter, const wchar_t* string_value,
    bool* output) {
  if (!output) return false;
  *output = false;
  ReflectedCall call;
  if (!PrepareExactCall(object, function, &call) || !string_parameter ||
      !string_value || FunctionParameterCount(call.function) != 2)
    return false;
  void* input_field = FindCallField(call, string_parameter);
  void* return_field = FindCallField(call, L"ReturnValue");
  if (!input_field || !return_field ||
      !UnrealObjectIsA(input_field, L"StrProperty") ||
      !ValidInputParameter(input_field) || FieldArrayDim(input_field) != 1 ||
      FieldElementSize(input_field) != sizeof(UnrealArrayView) ||
      !UnrealObjectIsA(return_field, L"BoolProperty") ||
      !Readable(static_cast<uint8_t*>(return_field) + 0x38,
                sizeof(uint32_t)))
    return false;
  const uint32_t return_flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(return_field) + 0x38);
  const uint32_t input_offset = FieldOffset(input_field);
  const uint32_t return_offset = FieldOffset(return_field);
  if ((return_flags & kPropertyReturnParm) == 0 || input_offset != 0 ||
      return_offset == UINT32_MAX || call.return_offset != return_offset ||
      !SetStringParameter(&call, string_parameter, string_value) ||
      !Execute(object, &call))
    return false;
  return ReadBoolReturn(call, output);
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
  // Resolving one parameter must not assume it is the entire signature.
  // SaveActiveCheckpoint also has a required completion delegate.
  if (offset == UINT32_MAX || offset + sizeof(int32_t) > parameter_size)
    return false;
  *output = offset;
  return true;
}

bool UnrealResolveExactDelegateParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output) {
  static_assert(sizeof(UnrealScriptDelegate) == 16, "WFC compiled delegate ABI");
  if (!output || !UnrealBridgeReady() || !ExactFunctionHandleValid(function))
    return false;
  *output = 0;
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  const uint16_t size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  if (!field || !UnrealObjectIsA(field, L"DelegateProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(UnrealScriptDelegate) ||
      FieldOffset(field) > size ||
      sizeof(UnrealScriptDelegate) > size - FieldOffset(field))
    return false;
  *output = FieldOffset(field);
  return true;
}

bool UnrealReadFrameDelegateParameter(void* frame, uint32_t offset,
                                      UnrealScriptDelegate* output) {
  if (!output || !Readable(frame, 0x14) ||
      offset > kMaxCallParameters - sizeof(*output))
    return false;
  const auto* locals = *reinterpret_cast<uint8_t**>(
      static_cast<uint8_t*>(frame) + 0x10);
  if (!locals || !Readable(locals + offset, sizeof(*output))) return false;
  std::memcpy(output, locals + offset, sizeof(*output));
  return ValidScriptDelegate(*output);
}

bool UnrealValidateExactIntDelegateCall(
    const UnrealFunctionHandle& function, const wchar_t* int_parameter,
    const wchar_t* delegate_parameter) {
  uint32_t int_offset = 0;
  uint32_t delegate_offset = 0;
  if (!UnrealResolveExactIntParameterOffset(function, int_parameter, &int_offset) ||
      !UnrealResolveExactDelegateParameterOffset(function, delegate_parameter,
                                                &delegate_offset))
    return false;
  const auto* bytes = static_cast<const uint8_t*>(function.function);
  return IntDelegateCallLayoutValid(FunctionParameterCount(function.function),
      *reinterpret_cast<const uint16_t*>(bytes + 0x5E),
      *reinterpret_cast<const uint16_t*>(bytes + 0x60), int_offset, delegate_offset);
}

bool UnrealResolveExactStringParameterOffset(
    const UnrealFunctionHandle& function, const wchar_t* parameter_name,
    uint32_t* output) {
  if (!output) return false;
  *output = 0;
  const auto fail = [&]() {
    Log(LogLevel::kError,
        "[Reflection] exact string parameter layout failed function-object=%lu parameter=%ls",
        static_cast<unsigned long>(function.function_object_index),
        parameter_name ? parameter_name : L"(null)");
    return false;
  };
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function) ||
      !parameter_name || !*parameter_name)
    return fail();
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"StrProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(UnrealArrayView))
    return fail();
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  if (offset == UINT32_MAX ||
      offset + sizeof(UnrealArrayView) > parameter_size)
    return fail();
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
  const auto fail = [&]() {
    Log(LogLevel::kError,
        "[Reflection] exact struct parameter layout failed function-object=%lu parameter=%ls expected-size=%lu",
        static_cast<unsigned long>(function.function_object_index),
        parameter_name ? parameter_name : L"(null)",
        static_cast<unsigned long>(expected_size));
    return false;
  };
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function) ||
      !parameter_name || !*parameter_name || !expected_size)
    return fail();
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, parameter_name);
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !ValidInputParameter(field) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != expected_size ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return fail();
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  if (!UnrealObjectValid(definition) ||
      ReflectedStructSize(definition) != expected_size ||
      offset == UINT32_MAX || offset + expected_size > parameter_size)
    return fail();
  *output_offset = offset;
  *output_struct_definition = definition;
  return true;
}

bool UnrealResolveExactStructReturnLayout(
    const UnrealFunctionHandle& function, uint32_t* output_size,
    void** output_struct_definition) {
  if (!output_size || !output_struct_definition) return false;
  *output_size = 0;
  *output_struct_definition = nullptr;
  const auto fail = [&]() {
    Log(LogLevel::kError,
        "[Reflection] exact struct return layout failed function-object=%lu",
        static_cast<unsigned long>(function.function_object_index));
    return false;
  };
  if (!UnrealBridgeReady() || !ExactFunctionHandleValid(function))
    return fail();
  ReflectedCall call;
  call.function = function.function;
  void* field = FindCallField(call, L"ReturnValue");
  if (!field || !UnrealObjectIsA(field, L"StructProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x38, sizeof(uint32_t)) ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)) ||
      FieldArrayDim(field) != 1)
    return fail();
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(field) + 0x38);
  void* definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  const uint32_t size = FieldElementSize(field);
  const uint32_t offset = FieldOffset(field);
  const uint16_t parameter_size = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x5E);
  const uint16_t return_offset = *reinterpret_cast<const uint16_t*>(
      static_cast<const uint8_t*>(function.function) + 0x60);
  if ((flags & kPropertyReturnParm) == 0 || !size || size > 0x10000 ||
      !UnrealObjectValid(definition) ||
      ReflectedStructSize(definition) != size || offset == UINT32_MAX ||
      offset + size > parameter_size || return_offset != offset)
    return fail();
  *output_size = size;
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
  const auto fail = [&](const char* reason) {
    Log(LogLevel::kError,
        "[Reflection] executor detour rejected reason=%s function-object=%lu active=%lu capacity=%lu",
        reason, static_cast<unsigned long>(function.function_object_index),
        static_cast<unsigned long>(g_function_exec_detour_count),
        static_cast<unsigned long>(g_function_exec_detours.size()));
    return false;
  };
  if (!output) return fail("missing-output");
  if (output->installed) return fail("output-already-installed");
  if (!UnrealBridgeReady()) return fail("bridge-not-ready");
  if (!ExactFunctionHandleValid(function)) return fail("invalid-function");
  if (!ExecutableExecutor(replacement_executor))
    return fail("replacement-not-executable");
  if (g_function_exec_detour_count >= g_function_exec_detours.size())
    return fail("registry-full");
  for (size_t index = 0; index < g_function_exec_detour_count; ++index) {
    if (g_function_exec_detours[index].function.function ==
        function.function)
      return fail("function-already-detoured");
  }

  volatile LONG* slot = nullptr;
  UnrealFunctionExec original_executor = nullptr;
  if (!EnsureFunctionExecutorInitialized(function, &slot,
                                         &original_executor))
    return fail("original-executor-unavailable");
  if (original_executor == replacement_executor)
    return fail("replacement-already-active");

  ActiveFunctionExecDetour active;
  active.function = function;
  active.original_executor = original_executor;
  active.replacement_executor = replacement_executor;
  active.engine_original_executor = original_executor;
  active.engine_replacement_executor = replacement_executor;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(function.function) + 0x54);
  if (flags & kFunctionNativeScript) {
    ScriptExecutorAdapters adapters;
    if (!AdaptCompiledScriptExecutors(original_executor, replacement_executor, &adapters))
      return fail("compiled-context-adapter");
    active.original_executor = adapters.normalized_original;
    active.engine_replacement_executor = adapters.engine_replacement;
  }
  // Publish the normalized original before making the replacement reachable.
  // Some engine callbacks can enter immediately after the atomic slot swap.
  output->function = function;
  output->original_executor = active.original_executor;
  output->replacement_executor = replacement_executor;
  output->engine_original_executor = active.engine_original_executor;
  output->engine_replacement_executor = active.engine_replacement_executor;
  output->installed = true;
  if (!CompareExchangeFunctionExecutor(function, active.engine_original_executor,
                                       active.engine_replacement_executor)) {
    *output = {};
    return fail("executor-compare-exchange");
  }
  g_function_exec_detours[g_function_exec_detour_count++] = active;
  return true;
}

bool UnrealRestoreFunctionExecDetour(UnrealFunctionExecDetour* detour) {
  if (!detour || !UnrealBridgeReady()) return false;
  const int active_index = FindActiveFunctionExecDetour(*detour);
  if (active_index < 0) return false;
  const ActiveFunctionExecDetour active =
      g_function_exec_detours[static_cast<size_t>(active_index)];
  if (!CompareExchangeFunctionExecutor(active.function,
                                       active.engine_replacement_executor,
                                       active.engine_original_executor))
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
                                        active.engine_replacement_executor,
                                        active.engine_original_executor))
      continue;
    restored_all = false;
    if (g_function_exec_detour_count < g_function_exec_detours.size())
      g_function_exec_detours[g_function_exec_detour_count++] = active;
  }
  return restored_all;
}

bool UnrealValidateScriptObjectOperand(
    const UnrealFunctionHandle& function, uint32_t opcode_offset,
    uint8_t expected_opcode, void* expected_object) {
  if (!UnrealBridgeReady() || !UnrealObjectValid(expected_object)) return false;
  uint8_t* operand = ScriptObjectOperandAddress(function, opcode_offset, expected_opcode);
  if (!operand) return false;
  void* observed = nullptr;
  std::memcpy(&observed, operand, sizeof(observed));
  return observed == expected_object;
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

bool UnrealReadDelegateProperty(void* object, const wchar_t* property_name,
                                UnrealScriptDelegate* output, uint32_t* offset) {
  if (!output) return false;
  *output = {};
  if (offset) *offset = 0;
  if (!UnrealObjectValid(object) || !property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"DelegateProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(*output)) return false;
  const auto field_offset = FieldOffset(field);
  if (field_offset == UINT32_MAX || field_offset > 0x100000 ||
      !Readable(static_cast<uint8_t*>(object) + field_offset, sizeof(*output))) return false;
  std::memcpy(output, static_cast<uint8_t*>(object) + field_offset, sizeof(*output));
  if (offset) *offset = field_offset;
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

bool UnrealResolveBytePropertyOffset(void* object,
                                     const wchar_t* property_name,
                                     uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealObjectValid(object) || !property_name || !*property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ByteProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != 1) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset < 0x28u ||
      !Readable(static_cast<uint8_t*>(object) + offset, 1)) return false;
  *output = offset;
  return true;
}

bool UnrealResolveArrayPropertyOffset(void* object, const wchar_t* property_name,
                                      uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealObjectValid(object) || !property_name || !*property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(UnrealArrayView)) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset < 0x28u ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(UnrealArrayView))) return false;
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

bool UnrealResolveFloatPropertyOffset(void* object,
                                      const wchar_t* property_name,
                                      uint32_t* output) {
  if (!output) return false;
  *output = 0;
  if (!UnrealObjectValid(object) || !property_name || !*property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"FloatProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(float)) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset < 0x28u || (offset & 3u) ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(float))) return false;
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

bool UnrealReadDelegateArrayProperty(void* object, const wchar_t* property_name,
                                     UnrealArrayView* output, uint32_t* offset) {
  if (offset) *offset = 0;
  if (!output) return false;
  *output = {};
  if (!UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(UnrealArrayView) ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*))) return false;
  void* inner = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x40);
  if (!UnrealObjectValid(inner) || !UnrealObjectIsA(inner, L"DelegateProperty") ||
      FieldArrayDim(inner) != 1 || FieldElementSize(inner) != sizeof(UnrealScriptDelegate)) return false;
  const uint32_t actual = FieldOffset(field);
  if (actual < 0x28 || actual == UINT32_MAX ||
      !Readable(static_cast<uint8_t*>(object) + actual, sizeof(UnrealArrayView))) return false;
  UnrealArrayView value;
  std::memcpy(&value, static_cast<uint8_t*>(object) + actual, sizeof(value));
  if (value.count < 0 || value.capacity < value.count || value.capacity > 4096 ||
      (value.capacity && !value.data) ||
      (value.count && !Readable(value.data,
          static_cast<size_t>(value.count) * sizeof(UnrealScriptDelegate)))) return false;
  *output = value;
  if (offset) *offset = actual;
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

bool UnrealStructArrayNameEquals(const UnrealStructArrayView& array,int32_t index,
                                 const wchar_t* field_name,const wchar_t* expected,bool* output) {
  if (!output) return false;
  *output=false;
  if (!field_name || !expected || !*expected || !array.data || array.count<0 ||
      array.count>4096 || array.capacity<array.count || array.capacity>4096) return false;
  uint8_t* element=nullptr;
  if (!StructElement(array,index,&element)) return false;
  void* field=FindStructField(array.struct_definition,field_name);
  if (!field || !UnrealObjectIsA(field,L"NameProperty") ||
      FieldArrayDim(field)!=1 || FieldElementSize(field)!=4) return false;
  const uint32_t offset=FieldOffset(field);
  if (offset>array.element_size || 4>array.element_size-offset || !Readable(element+offset,4)) return false;
  uint32_t actual=0,wanted=0;
  std::memcpy(&actual,element+offset,4);
  if (!MakeName(expected,&wanted)) return false;
  *output=actual==wanted;
  return true;
}

bool UnrealResolveClassByPath(const wchar_t* path,void** output) {
  if (!output) return false;
  *output=nullptr;
  if (!g_ready || !path || !std::wcschr(path,L'.')) return false;
  void* found=FindMetadataOwner(path);
  if (!UnrealObjectIsA(found,L"Class")) return false;
  *output=found; return true;
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

bool UnrealReadStructObjectField(const UnrealStructValueView& value,
                                 const wchar_t* field_name, void** output) {
  if (!output) return false;
  *output = nullptr;
  if (!value.data || !field_name || !UnrealObjectValid(value.struct_definition)) return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty") ||
      FieldElementSize(field) != sizeof(void*)) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset > value.size || sizeof(void*) > value.size - offset ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset, sizeof(void*))) return false;
  std::memcpy(output, static_cast<const uint8_t*>(value.data) + offset, sizeof(void*));
  return !*output || UnrealObjectValid(*output);
}

bool UnrealReadStructTypedArray(const UnrealStructValueView& value,
                                const wchar_t* field_name,
                                const wchar_t* inner_class, uint32_t element_size,
                                UnrealArrayView* output, uint32_t max_capacity) {
  if (!output) return false;
  *output = {};
  if (!value.data || !field_name || !inner_class || !element_size || element_size > 1024 ||
      !max_capacity || max_capacity > 65536 ||
      !UnrealObjectValid(value.struct_definition)) return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*))) return false;
  void* inner = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x40);
  if (!inner || !UnrealObjectIsA(inner, inner_class) || FieldElementSize(inner) != element_size)
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset > value.size || sizeof(UnrealArrayView) > value.size - offset ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset, sizeof(UnrealArrayView))) return false;
  UnrealArrayView array = {};
  std::memcpy(&array, static_cast<const uint8_t*>(value.data) + offset, sizeof(array));
  if (array.count < 0 || array.capacity < array.count ||
      static_cast<uint32_t>(array.capacity) > max_capacity ||
      (array.count && (!array.data || !Readable(array.data, array.count * element_size)))) return false;
  *output = array;
  return true;
}

bool UnrealResolveStructFieldOffset(const UnrealStructValueView& value,
                                    const wchar_t* field_name,
                                    const wchar_t* field_class,
                                    uint32_t expected_size, uint32_t* output) {
  if (!output) return false;
  *output = UINT32_MAX;
  if (!field_name || !field_class || !expected_size || !value.data ||
      !UnrealObjectValid(value.struct_definition) || !Readable(value.data, value.size)) return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, field_class) ||
      FieldElementSize(field) != expected_size || FieldArrayDim(field) != 1) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset > value.size || expected_size > value.size - offset) return false;
  *output = offset;
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

bool UnrealReadStructFloatField(const UnrealStructValueView& value,
                                const wchar_t* field_name,float* output) {
  if (!output) return false;
  *output=0;
  if (!field_name || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition) || ReflectedStructSize(value.struct_definition)!=value.size) return false;
  void* field=FindStructField(value.struct_definition,field_name);
  if (!field || !UnrealObjectIsA(field,L"FloatProperty") || FieldArrayDim(field)!=1 ||
      FieldElementSize(field)!=sizeof(float)) return false;
  const uint32_t offset=FieldOffset(field);
  if (offset==UINT32_MAX || (offset&3) || offset>value.size || sizeof(float)>value.size-offset ||
      !Readable(static_cast<const uint8_t*>(value.data)+offset,sizeof(float))) return false;
  std::memcpy(output,static_cast<const uint8_t*>(value.data)+offset,sizeof(float));return true;
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

bool UnrealAssignStructIntField(const UnrealStructValueView& value,
                                const wchar_t* field_name, int32_t input) {
  if (!field_name || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"IntProperty") ||
      FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(input))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(input) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(input)))
    return false;
  std::memcpy(const_cast<uint8_t*>(
                  static_cast<const uint8_t*>(value.data)) + offset,
              &input, sizeof(input));
  return true;
}

bool UnrealAssignStructBoolField(const UnrealStructValueView& value,
                                 const wchar_t* field_name, bool input) {
  if (!field_name || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty") ||
      FieldArrayDim(field) != 1 ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t mask = *reinterpret_cast<const uint32_t*>(
      static_cast<const uint8_t*>(field) + 0x40);
  if (offset == UINT32_MAX || !mask ||
      offset + sizeof(uint32_t) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(uint32_t)))
    return false;
  uint32_t bits = 0;
  std::memcpy(&bits,
              static_cast<const uint8_t*>(value.data) + offset,
              sizeof(bits));
  bits = input ? bits | mask : bits & ~mask;
  std::memcpy(const_cast<uint8_t*>(
                  static_cast<const uint8_t*>(value.data)) + offset,
              &bits, sizeof(bits));
  return true;
}

bool UnrealCopyStructByteField(const UnrealStructValueView& source,
                               const UnrealStructValueView& destination,
                               const wchar_t* field_name) {
  if (!field_name || !source.data || !destination.data || !source.size ||
      source.size != destination.size ||
      source.struct_definition != destination.struct_definition ||
      !UnrealObjectValid(source.struct_definition))
    return false;
  void* field = FindStructField(source.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ByteProperty") ||
      FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(uint8_t))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(uint8_t) > source.size)
    return false;
  const auto* source_byte =
      static_cast<const uint8_t*>(source.data) + offset;
  auto* destination_byte =
      const_cast<uint8_t*>(static_cast<const uint8_t*>(destination.data)) +
      offset;
  if (!Readable(source_byte, sizeof(uint8_t)) ||
      !Readable(destination_byte, sizeof(uint8_t)))
    return false;
  *destination_byte = *source_byte;
  return true;
}

bool UnrealShrinkStructArray(const UnrealStructValueView& value,
                             const wchar_t* field_name,
                             int32_t new_count) {
  if (!field_name || new_count < 0 || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(UnrealArrayView) > value.size)
    return false;
  auto* array = reinterpret_cast<UnrealArrayView*>(
      const_cast<uint8_t*>(static_cast<const uint8_t*>(value.data)) + offset);
  if (!Readable(array, sizeof(*array)) || array->count < 0 ||
      array->capacity < array->count || array->capacity > 64 ||
      new_count > array->count)
    return false;
  auto* count = reinterpret_cast<volatile LONG*>(&array->count);
  const LONG observed = static_cast<LONG>(array->count);
  return InterlockedCompareExchange(count, static_cast<LONG>(new_count),
                                    observed) == observed;
}

bool UnrealReadStructByteArrayElement(const UnrealStructValueView& value,
                                      const wchar_t* field_name,
                                      int32_t index, uint8_t* output) {
  if (!output || !field_name || index < 0 || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* inner = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  if (!inner || !UnrealObjectIsA(inner, L"ByteProperty") ||
      FieldElementSize(inner) != sizeof(uint8_t))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(UnrealArrayView) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(UnrealArrayView)))
    return false;
  UnrealArrayView array = {};
  std::memcpy(&array, static_cast<const uint8_t*>(value.data) + offset,
              sizeof(array));
  if (array.count < 0 || array.capacity < array.count || array.count > 64 ||
      array.capacity > 64 || index >= array.count || !array.data ||
      !Readable(static_cast<const uint8_t*>(array.data) + index,
                sizeof(uint8_t)))
    return false;
  *output = *(static_cast<const uint8_t*>(array.data) + index);
  return true;
}

bool UnrealAssignStructStringArrayElementWide(
    const UnrealStructValueView& value, const wchar_t* field_name,
    int32_t index, const wchar_t* text) {
  if (!g_fstring_assign || !field_name || !text || index < 0 || !value.data ||
      !value.size || !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
    return false;
  void* inner = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(field) + 0x40);
  if (!inner || !UnrealObjectIsA(inner, L"StrProperty") ||
      FieldElementSize(inner) != sizeof(UnrealArrayView))
    return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(UnrealArrayView) > value.size ||
      !Readable(static_cast<const uint8_t*>(value.data) + offset,
                sizeof(UnrealArrayView)))
    return false;
  UnrealArrayView array = {};
  std::memcpy(&array, static_cast<const uint8_t*>(value.data) + offset,
              sizeof(array));
  if (array.count < 0 || array.capacity < array.count || array.count > 64 ||
      array.capacity > 64 || index >= array.count || !array.data)
    return false;
  auto* destination = reinterpret_cast<UnrealArrayView*>(
      static_cast<uint8_t*>(array.data) +
      static_cast<size_t>(index) * sizeof(UnrealArrayView));
  return UnrealAssignWideStringValue(destination, text);
}

bool UnrealApplyOwnedStringArrayOverrides(
    const UnrealStructValueView& value,
    const UnrealStringArrayOverride* overrides, size_t count) {
  if (!count) return true;
  if (!g_array_resize_allocation || !g_fstring_assign || !overrides ||
      count > 4 || !value.data || !value.size ||
      !UnrealObjectValid(value.struct_definition)) return false;
  struct PendingArray {
    UnrealArrayView* destination = nullptr;
    UnrealArrayView original = {};
    UnrealArrayView staged = {};
  };
  std::array<PendingArray, 4> pending = {};
  // Zero-count FString assignment destroys its allocation through WFC's
  // allocator. Then release the TArray backing block through the same engine.
  const auto destroy = [](UnrealArrayView& array) {
    const UnrealArrayView empty = {};
    auto* strings = static_cast<UnrealArrayView*>(array.data);
    for (int32_t i = 0; i < array.count; ++i)
      WfcAssignFString(g_fstring_assign, &strings[i], &empty);
    g_array_resize_allocation(&array, nullptr, array.count, 0,
                               sizeof(UnrealArrayView));
    array = {};
  };
  const auto fail = [&]() {
    for (auto& item : pending) destroy(item.staged);
    return false;
  };
  for (size_t i = 0; i < count; ++i) {
    const auto& edit = overrides[i];
    if (!edit.field_name || edit.count < 0 || edit.count > 64 ||
        (edit.count && !edit.elements)) return fail();
    void* field = FindStructField(value.struct_definition, edit.field_name);
    if (!field || !UnrealObjectIsA(field, L"ArrayProperty") ||
        !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(void*)))
      return fail();
    void* inner = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x40);
    const uint32_t offset = FieldOffset(field);
    if (!inner || !UnrealObjectIsA(inner, L"StrProperty") ||
        FieldElementSize(inner) != sizeof(UnrealArrayView) ||
        offset == UINT32_MAX || offset > value.size ||
        sizeof(UnrealArrayView) > value.size - offset ||
        !Readable(static_cast<const uint8_t*>(value.data) + offset,
                  sizeof(UnrealArrayView))) return fail();
    auto& item = pending[i];
    item.destination = reinterpret_cast<UnrealArrayView*>(
        const_cast<uint8_t*>(static_cast<const uint8_t*>(value.data)) + offset);
    for (size_t j = 0; j < i; ++j)
      if (pending[j].destination == item.destination) return fail();
    item.original = *item.destination;
    const auto& old = item.original;
    if (old.count < 0 || old.capacity < old.count || old.capacity > 64 ||
        (old.count && (!old.data ||
            !Readable(old.data, old.count * sizeof(UnrealArrayView)))))
      return fail();
    // Borrowed or aliased arrays are not valid inputs to this owned-result API.
    for (size_t j = 0; j < i; ++j)
      if (old.data && old.data == pending[j].original.data) return fail();
    const auto* old_strings = static_cast<const UnrealArrayView*>(old.data);
    for (int32_t j = 0; j < old.count; ++j) {
      const auto& str = old_strings[j];
      if (str.count < 0 || str.capacity < str.count || str.capacity > 4096 ||
          (str.count && (!str.data || !Readable(str.data, str.count * sizeof(wchar_t)) ||
              static_cast<const wchar_t*>(str.data)[str.count - 1] != L'\0')))
        return fail();
    }
    const int32_t new_count = edit.preserve_tail
        ? std::max(old.count, edit.count) : edit.count;
    if (!new_count) continue;
    g_array_resize_allocation(&item.staged, nullptr, 0, new_count,
                               sizeof(UnrealArrayView));
    if (!item.staged.data) return fail();
    std::memset(item.staged.data, 0, new_count * sizeof(UnrealArrayView));
    item.staged.count = item.staged.capacity = new_count;
    auto* strings = static_cast<UnrealArrayView*>(item.staged.data);
    for (int32_t j = 0; j < new_count; ++j) {
      if (j < edit.count) {
        if (!edit.elements[j] ||
            !UnrealAssignWideStringValue(&strings[j], edit.elements[j]))
          return fail();
      } else if (WfcAssignFString(g_fstring_assign, &strings[j],
                                  &old_strings[j]) != &strings[j]) {
        return fail();
      }
    }
  }
  // No callbacks or engine mutations between the final identity check and
  // publication. Callers run this on the game thread, on owned return values.
  for (size_t i = 0; i < count; ++i)
    if (std::memcmp(pending[i].destination, &pending[i].original,
                    sizeof(UnrealArrayView)) != 0) return fail();
  for (size_t i = 0; i < count; ++i) {
    *pending[i].destination = pending[i].staged;
    pending[i].staged = {};
  }
  for (size_t i = 0; i < count; ++i) destroy(pending[i].original);
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
  if (!UnrealObjectReferenceWritesReady() ||
      !UnrealObjectValid(object) || !property_name ||
      (expected_value && !UnrealObjectValid(expected_value)) ||
      (new_value && !UnrealObjectValid(new_value)))
    return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"ObjectProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || (offset & 3u) || FieldArrayDim(field) != 1 ||
      FieldElementSize(field) != sizeof(void*) ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(void*)))
    return false;
  auto** slot = reinterpret_cast<void**>(
      static_cast<uint8_t*>(object) + offset);
  // Readable() intentionally caches read permission, not write permission.
  // This is a low-frequency handoff write; query the current page protection.
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(slot, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
  const bool written = InterlockedCompareExchangePointer(
             reinterpret_cast<void* volatile*>(slot), new_value,
             expected_value) == expected_value;
  // Match shipping compiled assignment, including GC notification AFTER the
  // write. Atomic pointer publication alone does not preserve GC reachability.
  // The native barrier cannot dispatch script or re-enter the transaction.
  if (written) g_object_reference_barrier(new_value);
  return written;
}

bool UnrealObjectReferenceWritesReady() {
  return g_ready && g_object_reference_barrier && g_reference_write_thread &&
      GetCurrentThreadId() == g_reference_write_thread;
}

#include "UnrealObjectClone.inc"

bool UnrealWriteBoolPropertyExpected(void* object,
                                     const wchar_t* property_name,
                                     bool expected_value, bool new_value) {
  // Campaign menu mode changes are live game mutations, just like ownership
  // writes. Reject other threads before accessing the reflection caches. A
  // Boolean does not contain a UObject reference and needs no GC barrier.
  if (!g_ready || !g_reference_write_thread ||
      GetCurrentThreadId() != g_reference_write_thread ||
      !UnrealObjectValid(object) || !property_name) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty") ||
      !Readable(static_cast<uint8_t*>(field) + 0x40, sizeof(uint32_t)))
    return false;
  const uint32_t offset = FieldOffset(field);
  const uint32_t mask = *reinterpret_cast<const uint32_t*>(
      static_cast<uint8_t*>(field) + 0x40);
  if (offset == UINT32_MAX || offset < 0x28u || (offset & 3u) ||
      FieldArrayDim(field) != 1 || FieldElementSize(field) != sizeof(LONG) ||
      !mask || (mask & (mask - 1u)) ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(LONG)))
    return false;
  auto* slot = reinterpret_cast<volatile LONG*>(
      static_cast<uint8_t*>(object) + offset);
  // Even the initial compare/exchange is a write instruction. Readable()
  // caches read permissions; check current protection before either CAS.
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(const_cast<const LONG*>(slot), &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
    return false;
  const LONG observed = InterlockedCompareExchange(slot, 0, 0);
  if (((static_cast<uint32_t>(observed) & mask) != 0) != expected_value)
    return false;
  const LONG desired = static_cast<LONG>(
      new_value ? static_cast<uint32_t>(observed) | mask
                : static_cast<uint32_t>(observed) & ~mask);
  return InterlockedCompareExchange(slot, desired, observed) == observed;
}

bool UnrealWriteIntPropertyExpected(void* object, const wchar_t* property_name,
                                    int32_t expected_value, int32_t new_value) {
  if (!g_ready || !g_reference_write_thread || GetCurrentThreadId()!=g_reference_write_thread ||
      !UnrealObjectValid(object) || !property_name) return false;
  void* field=FindField(object,property_name);
  if (!field || !UnrealObjectIsA(field,L"IntProperty") || FieldArrayDim(field)!=1 ||
      FieldElementSize(field)!=sizeof(LONG)) return false;
  const uint32_t offset=FieldOffset(field);
  if (offset==UINT32_MAX || offset<0x28 || offset>0x100000 || (offset&3)) return false;
  auto* slot=reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(object)+offset);
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(const_cast<const LONG*>(slot),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
      (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
      sizeof(LONG)>info.RegionSize-(reinterpret_cast<uintptr_t>(slot)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  return InterlockedCompareExchange(slot,new_value,expected_value)==expected_value;
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

  const bool has_second_pri = second_pri != nullptr;

  if (sizeof(void*) != 4 || !g_ready || !UnrealObjectValid(first_pri) ||
      (has_second_pri &&
       (!UnrealObjectValid(second_pri) || first_pri == second_pri)) ||
      !expected_game_thread || GetCurrentThreadId() != expected_game_thread ||
      !first_character.data ||
      (has_second_pri &&
       (!second_character.data ||
        first_character.struct_definition !=
            second_character.struct_definition ||
        first_character.size != second_character.size)) ||
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
      !UnrealReadStructProperty(first_pri, L"ReplicatedCharacterData",
                                &first_replication) ||
      !UnrealReadStructIntField(first_replication, L"ForceReplicationFlag",
                                &first_replication_flag) ||
      (has_second_pri &&
       (!UnrealReadArrayProperty(second_pri, L"LoadCompletePlayers",
                                 &second_load_observers) ||
        !UnrealReadStructProperty(second_pri, L"ReplicatedCharacterData",
                                  &second_replication) ||
        !UnrealReadStructIntField(second_replication,
                                  L"ForceReplicationFlag",
                                  &second_replication_flag)))) {
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
  if (valid &&
      Readable(function_bytes + kFunctionScriptDataOffset,
               sizeof(script) + sizeof(script_count)) &&
      !(*reinterpret_cast<const uint32_t*>(function_bytes + 0x54) & kFunctionNativeScript)) {
    std::memcpy(&script, function_bytes + kFunctionScriptDataOffset,
                sizeof(script));
    std::memcpy(&script_count, function_bytes + kFunctionScriptCountOffset,
                sizeof(script_count));
  } else {
    valid = false;
  }
  auto* bytes = static_cast<uint8_t*>(script);
  void* original_client_operand = nullptr;
  if (valid) {
    valid = script_count == kExpectedScriptSize &&
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
          (!has_second_pri ||
           UnrealCallExactVoidStruct(second_pri, select_character, L"CD",
                                     second_character));
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
        (!has_second_pri ||
         UnrealCallExactVoidStruct(second_pri, confirm_selected_character,
                                   L"SelectedCharacter", second_character));
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
      first_load_after.data == first_load_observers.data &&
      first_load_after.count == first_load_observers.count &&
      first_load_after.capacity == first_load_observers.capacity &&
      UnrealReadStructProperty(first_pri, L"ReplicatedCharacterData",
                               &first_replication_after) &&
      UnrealReadStructIntField(first_replication_after,
                               L"ForceReplicationFlag", &first_flag_after) &&
      first_flag_after == first_replication_flag &&
      (!has_second_pri ||
       (UnrealReadArrayProperty(second_pri, L"LoadCompletePlayers",
                                &second_load_after) &&
        second_load_after.data == second_load_observers.data &&
        second_load_after.count == second_load_observers.count &&
        second_load_after.capacity == second_load_observers.capacity &&
        UnrealReadStructProperty(second_pri, L"ReplicatedCharacterData",
                                 &second_replication_after) &&
        UnrealReadStructIntField(second_replication_after,
                                 L"ForceReplicationFlag",
                                 &second_flag_after) &&
        second_flag_after == second_replication_flag));

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

bool UnrealSynchronouslyAssignCharacter(
    void* pri, const UnrealFunctionHandle& select_character,
    const UnrealFunctionHandle& client_start_character_load,
    const UnrealFunctionHandle& confirm_selected_character,
    const UnrealFunctionHandle& refresh_provider,
    const UnrealStructValueView& character,
    uint32_t expected_game_thread, bool* mutation_started) {
  return UnrealSynchronouslyAssignCharacterPair(
      pri, nullptr, select_character, client_start_character_load,
      confirm_selected_character, refresh_provider, character, character,
      expected_game_thread, mutation_started);
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

bool UnrealReadFrameStringParameter(void* frame, uint32_t parameter_offset,
                                    char* output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = '\0';
  if (!frame || !Readable(static_cast<uint8_t*>(frame) + 0x10,
                          sizeof(void*)))
    return false;
  void* locals = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(frame) + 0x10);
  if (!locals || !Readable(static_cast<uint8_t*>(locals) + parameter_offset,
                           sizeof(UnrealArrayView)))
    return false;
  UnrealArrayView value = {};
  std::memcpy(&value, static_cast<uint8_t*>(locals) + parameter_offset,
              sizeof(value));
  return CopyUnrealString(value, output, capacity);
}

bool UnrealAssignFrameStringParameter(void* frame, uint32_t parameter_offset,
                                      const wchar_t* value) {
  if (!frame || !value ||
      !Readable(static_cast<uint8_t*>(frame) + 0x10, sizeof(void*)))
    return false;
  void* locals = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(frame) + 0x10);
  return locals &&
         UnrealAssignWideStringValue(
             static_cast<uint8_t*>(locals) + parameter_offset, value);
}

bool UnrealReadFrameStructParameter(
    void* frame, uint32_t parameter_offset, uint32_t parameter_size,
    void* struct_definition, UnrealStructValueView* output) {
  if (!output) return false;
  *output = {};
  if (!frame || !parameter_size || parameter_size > 0x10000 ||
      !UnrealObjectValid(struct_definition) ||
      ReflectedStructSize(struct_definition) != parameter_size ||
      !Readable(static_cast<uint8_t*>(frame) + 0x10, sizeof(void*)))
    return false;
  void* locals = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(frame) + 0x10);
  if (!locals ||
      !Readable(static_cast<uint8_t*>(locals) + parameter_offset,
                parameter_size))
    return false;
  output->data = static_cast<uint8_t*>(locals) + parameter_offset;
  output->size = parameter_size;
  output->struct_definition = struct_definition;
  return true;
}

bool UnrealReadStringValue(const void* value, char* output,
                           size_t capacity) {
  if (!value || !Readable(value, sizeof(UnrealArrayView))) return false;
  UnrealArrayView string_value = {};
  std::memcpy(&string_value, value, sizeof(string_value));
  return CopyUnrealString(string_value, output, capacity);
}

bool UnrealAssignWideStringValue(void* destination, const wchar_t* value) {
  if (!g_fstring_assign || !destination || !value ||
      !Readable(destination, sizeof(UnrealArrayView)))
    return false;
  const size_t length = std::wcslen(value);
  if (length > 4095) return false;
  const UnrealArrayView source = {
      const_cast<wchar_t*>(value), static_cast<int32_t>(length + 1),
      static_cast<int32_t>(length + 1)};
  return WfcAssignFString(g_fstring_assign, destination, &source) ==
         destination;
}

bool UnrealAssignStructWideStringField(const UnrealStructValueView& value,
                                       const wchar_t* field_name,
                                       const wchar_t* text) {
  if (!g_fstring_assign || !value.data || !value.size || !field_name ||
      !text || !UnrealObjectValid(value.struct_definition))
    return false;
  void* field = FindStructField(value.struct_definition, field_name);
  if (!field || !UnrealObjectIsA(field, L"StrProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX || offset + sizeof(UnrealArrayView) > value.size)
    return false;
  return UnrealAssignWideStringValue(
      const_cast<uint8_t*>(static_cast<const uint8_t*>(value.data)) + offset,
      text);
}

bool UnrealSetVariableUndefined(void* movie, const wchar_t* path) {
  if (!movie || !path || !*path) return false;
  ReflectedCall call;
  if (!PrepareCall(movie, L"SetVariable", &call) ||
      FunctionParameterCount(call.function) != 2 ||
      call.return_offset != 0xFFFFu)
    return false;
  void* path_field = FindCallField(call, L"Path");
  void* value_field = FindCallField(call, L"arg");
  if (!path_field || !value_field ||
      !UnrealObjectIsA(path_field, L"StrProperty") ||
      !ValidInputParameter(path_field) || FieldArrayDim(path_field) != 1 ||
      FieldElementSize(path_field) != sizeof(UnrealArrayView) ||
      !UnrealObjectIsA(value_field, L"StructProperty") ||
      !ValidInputParameter(value_field) || FieldArrayDim(value_field) != 1 ||
      !Readable(static_cast<uint8_t*>(value_field) + 0x40, sizeof(void*)))
    return false;
  void* value_definition = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(value_field) + 0x40);
  const uint32_t value_offset = FieldOffset(value_field);
  const uint32_t value_size = FieldElementSize(value_field);
  void* type_field = FindStructField(value_definition, L"Type");
  if (!UnrealObjectValid(value_definition) || !value_size ||
      value_size > 0x100 ||
      ReflectedStructSize(value_definition) != value_size ||
      value_offset == UINT32_MAX || value_offset + value_size > call.size ||
      !type_field || !UnrealObjectIsA(type_field, L"ByteProperty") ||
      FieldArrayDim(type_field) != 1 || FieldElementSize(type_field) != 1 ||
      FieldOffset(type_field) != 0 ||
      !SetStringParameter(&call, L"Path", path))
    return false;
  // ASValue's reflected ASType byte is first, and AS_Undefined is value zero.
  // ReflectedCall storage begins zeroed, so the nested FString/object/function
  // fields also have valid empty representations and require no destruction.
  std::memset(call.parameters.data() + value_offset, 0, value_size);
  return Execute(movie, &call);
}

bool UnrealValidateCallContract(
    void* object, const wchar_t* function_name,
    const wchar_t* return_property_class,
    std::initializer_list<UnrealCallParameterContract> inputs) {
  ReflectedCall call;
  bool ok = PrepareCall(object, function_name, &call);
  if (ok) {
    for (const auto& input : inputs) {
      void* field = FindCallField(call, input.name);
      ok = field && UnrealObjectIsA(field, input.property_class) &&
           ValidInputParameter(field) && FieldArrayDim(field) == 1 &&
           (!input.size || FieldElementSize(field) == input.size) &&
           FieldOffset(field) <= call.size &&
           FieldElementSize(field) <= call.size - FieldOffset(field) && ok;
    }
    void* field = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(call.function) + 0x30);
    size_t fields = 0;
    size_t parameters = 0;
    bool saw_return = false;
    for (; field && fields < 128; ++fields) {
      if (!Readable(field, 0x44)) { ok = false; break; }
      const uint32_t flags = *reinterpret_cast<const uint32_t*>(
          static_cast<const uint8_t*>(field) + 0x38);
      if (flags & kPropertyParm) {
        ++parameters;
        if (flags & kPropertyReturnParm) {
          saw_return = true;
          ok = return_property_class &&
               UnrealObjectIsA(field, return_property_class) &&
               FieldOffset(field) == call.return_offset &&
               FieldOffset(field) <= call.size &&
               FieldElementSize(field) <= call.size - FieldOffset(field) && ok;
        } else {
          bool supplied = false;
          for (const auto& input : inputs)
            supplied = FindCallField(call, input.name) == field || supplied;
          // Never silently default a newly added required argument or an out
          // parameter. Optional input defaults use the engine's zeroed frame.
          ok = !(flags & kPropertyOutParm) &&
               (supplied || (flags & 0x10u)) && ok;
        }
      }
      field = *reinterpret_cast<void**>(static_cast<uint8_t*>(field) + 0x2C);
    }
    ok = !field && parameters == FunctionParameterCount(call.function) &&
         saw_return == (return_property_class != nullptr) &&
         (saw_return || call.return_offset == 0xFFFFu) && ok;
  }
  if (!ok) {
    char owner[96] = {};
    UnrealObjectClassName(object, owner, sizeof(owner));
    Log(LogLevel::kWarning,
        "[SquadSwitch] call contract failed class=%s function=%ls resolved=%p size=%u return=%u",
        owner, function_name, call.function, call.size, call.return_offset);
  }
  return ok;
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

bool UnrealCallBoolString(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const wchar_t* value, bool* output) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         SetStringParameter(&call, parameter_name, value) &&
         Execute(object, &call) && ReadBoolReturn(call, output);
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
         call.size >= sizeof(value) && call.size <= sizeof(uint32_t) &&
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

bool UnrealCallVoidString(void* object, const wchar_t* function_name,
                          const wchar_t* parameter_name,
                          const wchar_t* value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         call.return_offset == 0xFFFFu &&
         SetStringParameter(&call, parameter_name, value) &&
         Execute(object, &call);
}

bool UnrealCallVoidStringString(
    void* object, const wchar_t* function_name,
    const wchar_t* first_parameter, const wchar_t* first_value,
    const wchar_t* second_parameter, const wchar_t* second_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.return_offset == 0xFFFFu &&
         SetStringParameter(&call, first_parameter, first_value) &&
         SetStringParameter(&call, second_parameter, second_value) &&
         Execute(object, &call);
}

bool UnrealCallVoidStringBool(
    void* object, const wchar_t* function_name,
    const wchar_t* string_parameter, const wchar_t* string_value,
    const wchar_t* bool_parameter, bool bool_value) {
  ReflectedCall call;
  return PrepareCall(object, function_name, &call) &&
         FunctionParameterCount(call.function) == 2 &&
         call.return_offset == 0xFFFFu &&
         SetStringParameter(&call, string_parameter, string_value) &&
         SetBoolParameter(&call, bool_parameter, bool_value) &&
         Execute(object, &call);
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
