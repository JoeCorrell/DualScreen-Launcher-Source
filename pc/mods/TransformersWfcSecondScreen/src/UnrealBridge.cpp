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
constexpr uintptr_t kFStringAssignRva = 0x00001150u;
constexpr size_t kFindFunctionVtableOffset = 0xC0u;
constexpr size_t kMaxCallParameters = 1024;
constexpr int32_t kMaxObjects = 500000;
constexpr int32_t kMaxNames = 262143;
// A live snapshot touches objects from many UE3 allocations. Keeping enough
// committed regions for the whole pulse avoids repeating VirtualQuery hundreds
// of times under Wine/Box64, where it is particularly expensive.
constexpr size_t kMemoryRegionCacheCapacity = 128;
constexpr size_t kNameCacheCapacity = 128;
constexpr size_t kLookupCacheCapacity = 512;
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
void* g_invoke = nullptr;
void* g_fstring_assign = nullptr;
bool g_ready = false;

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

void ClearReflectionCaches() {
  g_memory_region_cache_count = 0;
  g_name_cache_count = 0;
  g_field_cache_count = 0;
  g_struct_field_cache_count = 0;
  g_call_field_cache_count = 0;
  g_is_a_cache_count = 0;
}

bool FindCachedLookup(
    const std::array<LookupCacheEntry, kLookupCacheCapacity>& cache,
    size_t count, void* context, uint32_t name_index, void** result) {
  for (size_t index = 0; index < count; ++index) {
    if (cache[index].context == context &&
        cache[index].name_index == name_index) {
      *result = cache[index].result;
      return true;
    }
  }
  return false;
}

void CacheLookup(std::array<LookupCacheEntry, kLookupCacheCapacity>* cache,
                 size_t* count, void* context, uint32_t name_index,
                 void* result) {
  if (!cache || !count || *count >= cache->size()) return;
  (*cache)[(*count)++] = {context, name_index, result};
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

bool MakeName(const wchar_t* text, uint32_t* output) {
  if (!g_ready || !text || !*text || !output) return false;
  for (size_t index = 0; index < g_name_cache_count; ++index) {
    if (g_name_cache[index].literal == text) {
      *output = g_name_cache[index].packed;
      return true;
    }
  }
  *output = 0;
  // findType=0 is the engine's non-mutating FNAME_Find path.
  g_make_name(output, text, 0, 0, 1);
  if (!*output) return false;
  if (g_name_cache_count < g_name_cache.size())
    g_name_cache[g_name_cache_count++] = {text, *output};
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
      !UnrealObjectValid(object) || !Executable(g_invoke))
    return false;
  WfcInvokeReflected(g_invoke, call->function, object,
                     call->parameters.data());
  return true;
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
  void* fstring_assign = base + kFStringAssignRva;
  if (!Readable(make_name, sizeof(kMakeFNamePrologue)) ||
      std::memcmp(make_name, kMakeFNamePrologue,
                  sizeof(kMakeFNamePrologue)) != 0 ||
      !Readable(invoke, sizeof(kInvokePrologue)) ||
      std::memcmp(invoke, kInvokePrologue, sizeof(kInvokePrologue)) != 0 ||
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
  g_ready = false;
  ClearReflectionCaches();
  g_invoke = nullptr;
  g_fstring_assign = nullptr;
  g_make_name = nullptr;
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
  // regions only for this post-Tick pulse so repeated validation remains safe
  // across level transitions while avoiding hundreds of duplicate queries.
  g_memory_region_cache_count = 0;
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
  for (size_t index = 0; index < g_is_a_cache_count; ++index) {
    if (g_is_a_cache[index].object_class == object_class &&
        g_is_a_cache[index].target_name_index == target_name_index)
      return g_is_a_cache[index].result;
  }
  bool result = false;
  for (int depth = 0; current && depth < 128; ++depth) {
    if (BaseNameIndexEquals(current, target_name_index)) {
      result = true;
      break;
    }
    current = ClassParent(current);
  }
  if (g_is_a_cache_count < g_is_a_cache.size())
    g_is_a_cache[g_is_a_cache_count++] = {
        object_class, target_name_index, result};
  return result;
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

bool UnrealReadBoolProperty(void* object, const wchar_t* property_name,
                            bool* output) {
  if (!output || !UnrealObjectValid(object)) return false;
  void* field = FindField(object, property_name);
  if (!field || !UnrealObjectIsA(field, L"BoolProperty")) return false;
  const uint32_t offset = FieldOffset(field);
  if (offset == UINT32_MAX ||
      !Readable(static_cast<uint8_t*>(object) + offset, sizeof(uint32_t)))
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
