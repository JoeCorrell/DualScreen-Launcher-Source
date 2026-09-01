#include "Rtti.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "Log.h"
#include "Mem.h"

namespace dis {
namespace {

Rtti g_rtti;

// Taken from the export table of engine_x64_rwdi.dll (11,481,024 bytes,
// 2016-05-31), not written from memory. All fifteen are present in that build.
//
// Read the mangling before changing any of these. `QEBA` is a non-virtual
// public const member and `QEAA` its non-const twin; both are safe to call at
// the exported address. `UEBA` would be a virtual, and none of these are --
// that is the property the whole file depends on.
constexpr const char kObjClassName[]  = "?GetClassNameA@CRTTIObject@@QEBAPEBDXZ";
constexpr const char kObjFindField[]  = "?FindField@CRTTIObject@@QEBAPEBVCRTTIField@@PEBD@Z";
constexpr const char kObjGetFloat[]   = "?GetFieldFloat@CRTTIObject@@QEBAPEBVCRTTIFieldFloat@@PEBDAEAM@Z";
constexpr const char kObjGetInt[]     = "?GetFieldInt@CRTTIObject@@QEBAPEBVCRTTIFieldInt@@PEBDAEAH@Z";
constexpr const char kObjGetBool[]    = "?GetFieldBool@CRTTIObject@@QEBAPEBVCRTTIFieldBool@@PEBDAEA_N@Z";
constexpr const char kObjGetString[]  = "?GetFieldString@CRTTIObject@@QEBAPEBVCRTTIFieldString@@PEBDAEAV?$string_base@D@ttl@@@Z";
constexpr const char kObjGetDouble[]  = "?GetFieldDouble@CRTTIObject@@QEBAPEBVCRTTIFieldDouble@@PEBDAEAN@Z";
constexpr const char kObjGetEnum[]    = "?GetFieldEnum@CRTTIObject@@QEBAPEBVCRTTIFieldEnum@@PEBDAEAH@Z";
constexpr const char kObjSetFloat[]   = "?SetFieldFloat@CRTTIObject@@QEAAPEBVCRTTIFieldFloat@@PEBDM@Z";
constexpr const char kObjSetInt[]     = "?SetFieldInt@CRTTIObject@@QEAAPEBVCRTTIFieldInt@@PEBDH@Z";
constexpr const char kObjSetBool[]    = "?SetFieldBool@CRTTIObject@@QEAAPEBVCRTTIFieldBool@@PEBD_N@Z";
constexpr const char kClassGetFields[]= "?GetFields@CRTTI@@QEBAAEBV?$vector@PEBVCRTTIField@@@ttl@@XZ";
constexpr const char kClassGetBase[]  = "?GetBaseClass@CRTTI@@QEBAPEBV1@PEBV1@@Z";
constexpr const char kClassFindField[]= "?FindField@CRTTI@@QEBAPEBVCRTTIField@@PEBD@Z";
constexpr const char kFieldGetName[]  = "?GetName@CRTTIField@@QEBAPEBDXZ";
constexpr const char kFieldGetType[]  = "?GetType@CRTTIField@@QEBA?AW4ERTTIType@@XZ";
constexpr const char kClassName[]     = "?GetClassNameA@CRTTI@@UEBAPEBDXZ";
constexpr const char kObjGetAll[]     = "?GetAllObjects@CRTTIObject@@UEAAXAEAV?$vector@PEAVIGSObject@@@ttl@@_N@Z";
constexpr const char kObjRtti[]       = "?GetRTTI@CRTTIObject@@UEBAPEBVCRTTI@@XZ";
constexpr const char kObjEngineRtti[] = "?GetEngineRTTI@CRTTIObject@@UEBAPEBVCRTTI@@XZ";
constexpr const char kObjVtable[]     = "??_7CRTTIObject@@6B@";

// mingw-w64 on x86_64 uses the Microsoft ABI, so an exported member function is
// callable as a plain function with `this` first and no thunk. The GetField*
// family returns the descriptor it matched, or null when the class has no such
// field -- that return value, not the out-parameter, is the success test.
using FnClassName  = const char* (*)(const void*);
using FnFindField  = const void* (*)(const void*, const char*);
using FnGetFloat   = const void* (*)(const void*, const char*, float*);
using FnGetInt     = const void* (*)(const void*, const char*, int*);
using FnGetBool    = const void* (*)(const void*, const char*, bool*);
using FnGetDouble  = const void* (*)(const void*, const char*, double*);
using FnGetString  = const void* (*)(const void*, const char*, void*);
using FnSetFloat   = const void* (*)(void*, const char*, float);
using FnSetInt     = const void* (*)(void*, const char*, int);
using FnSetBool    = const void* (*)(void*, const char*, bool);
using FnGetRaw     = const void* (*)(const void*, const char*, void*);
using FnFieldName  = const char* (*)(const void*);
using FnFieldType  = int (*)(const void*);
using FnGetBase    = const CRTTI* (*)(const CRTTI*, const CRTTI*);

HMODULE g_module = nullptr;

// 64-bit MSVC stores image-relative RTTI records immediately before each
// vtable. These are all four-byte fields even in a 64-bit image. Reading them
// lets an interface pointer be adjusted to a named base class without calling
// a single method through the wrong sub-object first.
struct CompleteObjectLocator64 {
  uint32_t signature;
  uint32_t subobject_offset;
  uint32_t constructor_displacement;
  int32_t type_descriptor_rva;
  int32_t class_descriptor_rva;
  int32_t self_rva;
};

struct ClassHierarchyDescriptor64 {
  uint32_t signature;
  uint32_t attributes;
  uint32_t base_count;
  int32_t base_array_rva;
};

struct BaseClassDescriptor64 {
  int32_t type_descriptor_rva;
  uint32_t contained_bases;
  int32_t member_displacement;
  int32_t vbtable_displacement;
  int32_t virtual_displacement;
  uint32_t attributes;
  int32_t class_descriptor_rva;
};

static_assert(sizeof(CompleteObjectLocator64) == 24,
              "unexpected MSVC complete-object-locator layout");
static_assert(sizeof(ClassHierarchyDescriptor64) == 16,
              "unexpected MSVC class-hierarchy layout");
static_assert(sizeof(BaseClassDescriptor64) == 28,
              "unexpected MSVC base-class-descriptor layout");

bool ImageRange(const uint8_t* image, uint32_t image_size, int32_t rva,
                size_t bytes, const void** out) {
  if (!image || !out || rva <= 0) return false;
  const uint64_t start = static_cast<uint32_t>(rva);
  const uint64_t end = start + bytes;
  if (end < start || end > image_size) return false;
  const void* address = image + start;
  if (!Readable(address, bytes)) return false;
  *out = address;
  return true;
}

// ttl::string_base<char> keeps its characters behind a pointer, at an offset
// this mod has no documentation for. Rather than assume one, the first few
// words are tested for something that reads as a NUL-terminated ASCII string,
// and the inline case is tried last. A wrong guess would otherwise be a
// plausible-looking pointer into nothing.
std::string ReadTtlString(const void* s) {
  auto printable = [](const char* text) {
    if (!text || !*text) return false;
    for (const char* c = text; *c; ++c) {
      if (*c < 0x20 || *c > 0x7e) return false;
    }
    return true;
  };
  if (!s) return {};
  for (size_t off = 0; off <= 0x18; off += sizeof(void*)) {
    void* candidate = nullptr;
    if (!Read(static_cast<const uint8_t*>(s) + off, &candidate)) continue;
    if (const char* text = SafeCStr(candidate, 256)) {
      if (printable(text)) return std::string(text);
    }
  }
  if (const char* inline_text = SafeCStr(s, 64)) {
    if (printable(inline_text)) return std::string(inline_text);
  }
  return {};
}

}  // namespace

Rtti& TheRtti() { return g_rtti; }

void* Rtti::Find(const char* mangled, const char* pretty) {
  void* address = g_module
      ? reinterpret_cast<void*>(GetProcAddress(g_module, mangled)) : nullptr;
  symbols_.push_back(RttiSymbol{mangled, pretty, address});
  return address;
}

int Rtti::missing() const {
  int n = 0;
  for (const RttiSymbol& s : symbols_) if (!s.address) ++n;
  return n;
}

bool Rtti::Resolve(void* engine_module) {
  if (ready_) return true;
  g_module = static_cast<HMODULE>(engine_module);
  if (!g_module) return false;
  symbols_.clear();

  object_class_name_ = Find(kObjClassName,   "CRTTIObject::GetClassNameA");
  object_find_field_ = Find(kObjFindField,   "CRTTIObject::FindField");
  object_get_float_  = Find(kObjGetFloat,    "CRTTIObject::GetFieldFloat");
  object_get_int_    = Find(kObjGetInt,      "CRTTIObject::GetFieldInt");
  object_get_bool_   = Find(kObjGetBool,     "CRTTIObject::GetFieldBool");
  object_get_string_ = Find(kObjGetString,   "CRTTIObject::GetFieldString");
  object_get_double_ = Find(kObjGetDouble,   "CRTTIObject::GetFieldDouble");
  object_get_enum_   = Find(kObjGetEnum,     "CRTTIObject::GetFieldEnum");
  object_set_float_  = Find(kObjSetFloat,    "CRTTIObject::SetFieldFloat");
  object_set_int_    = Find(kObjSetInt,      "CRTTIObject::SetFieldInt");
  object_set_bool_   = Find(kObjSetBool,     "CRTTIObject::SetFieldBool");
  class_get_fields_  = Find(kClassGetFields, "CRTTI::GetFields");
  class_get_base_    = Find(kClassGetBase,   "CRTTI::GetBaseClass");
  class_find_field_  = Find(kClassFindField, "CRTTI::FindField");
  field_get_name_    = Find(kFieldGetName,   "CRTTIField::GetName");
  field_get_type_    = Find(kFieldGetType,   "CRTTIField::GetType");
  class_get_class_name_ = Find(kClassName,   "CRTTI::GetClassNameA");
  object_get_all_    = Find(kObjGetAll,      "CRTTIObject::GetAllObjects");
  object_get_rtti_ = Find(kObjRtti, "CRTTIObject::GetRTTI");
  object_get_engine_rtti_ = Find(kObjEngineRtti, "CRTTIObject::GetEngineRTTI");
  object_vtable_ = Find(kObjVtable, "CRTTIObject::vftable");

  // Find GetEngineRTTI's virtual slot from CRTTIObject's own exported
  // vftable, never from the object we happen to be inspecting. Gameplay
  // classes such as PlayerDI override this method, so their slot contains the
  // override and cannot possibly equal the exported base implementation.
  //
  // CRTTIObject declares GetRTTI immediately before GetEngineRTTI. In this
  // engine build the linker folded their identical base implementations, so
  // slots 2 and 3 contain the same address. The second member of that adjacent
  // pair is GetEngineRTTI. If a future build no longer folds them, the two
  // distinct exports identify the same adjacent pair unambiguously.
  rtti_slot_ = -1;
  engine_rtti_slot_ = -1;
  if (object_vtable_ && object_get_rtti_ && object_get_engine_rtti_) {
    auto* base_vtable = static_cast<void**>(object_vtable_);
    void* previous = nullptr;
    for (int slot = 0; slot < 32; ++slot) {
      void* entry = nullptr;
      if (!Read(base_vtable + slot, &entry)) break;
      const bool folded_pair = slot > 0 &&
          object_get_rtti_ == object_get_engine_rtti_ &&
          previous == object_get_rtti_ && entry == object_get_engine_rtti_;
      const bool distinct_pair = slot > 0 &&
          object_get_rtti_ != object_get_engine_rtti_ &&
          previous == object_get_rtti_ && entry == object_get_engine_rtti_;
      if (folded_pair || distinct_pair) {
        rtti_slot_ = slot - 1;
        engine_rtti_slot_ = slot;
        DIS_INFO("[rtti] GetRTTI/GetEngineRTTI are vtable slots %d/%d"
                 " (resolved from the exported CRTTIObject vftable)",
                 rtti_slot_, engine_rtti_slot_);
        break;
      }
      previous = entry;
    }
    if (engine_rtti_slot_ < 0) {
      DIS_WARN("[rtti] exported CRTTIObject vftable did not contain the"
               " expected adjacent GetRTTI/GetEngineRTTI pair; live-class"
               " enumeration is disabled");
    }
  }

  DIS_INFO("[rtti] %d/%d reflection symbols resolved",
           static_cast<int>(symbols_.size()) - missing(),
           static_cast<int>(symbols_.size()));
  for (const RttiSymbol& s : symbols_) {
    if (!s.address) DIS_ERR("[rtti]   MISSING %s", s.pretty);
  }

  // The four that the data layer cannot work without. The rest are reported
  // and then done without, so a build that dropped GetFieldString still gets
  // a HUD rather than nothing.
  ready_ = object_class_name_ && object_find_field_ && object_get_float_ &&
           object_get_int_;

  // Learn the container layout now rather than on first use. It needs only a
  // static class accessor, so it works before any level exists, and doing it
  // here means the answer is in the log next to the symbol table.
  if (ready_) LearnVectorLayout();
  return ready_;
}

// ------------------------------------------------------------------ identity
std::string Rtti::ClassName(const CRTTIObject* object) const {
  if (!object || !object_class_name_) return {};
  const char* name = reinterpret_cast<FnClassName>(object_class_name_)(object);
  const char* safe = SafeCStr(name, 128);
  if (!safe || !*safe) return {};

  // A class name that is not printable ASCII means this pointer is not a
  // CRTTIObject, and the bytes being read are something else entirely.
  //
  // This is not defensive padding -- it is the check that was missing. The
  // captured ILevel* was handed to the reflection API on the assumption that a
  // level is a CRTTIObject; CLevel inherits both interfaces, so the ILevel
  // sub-object sits at a different address than the CRTTIObject base, and
  // GetClassNameA happily returned garbage. The next call, FindField, took the
  // process down. An identifier is [A-Za-z_][A-Za-z0-9_:<>,\ ]* and anything
  // else is a wrong pointer, so it is rejected here rather than three calls
  // later.
  const unsigned char first = static_cast<unsigned char>(*safe);
  if (!(first == '_' || (first >= 'A' && first <= 'Z') ||
        (first >= 'a' && first <= 'z'))) {
    return {};
  }
  for (const char* c = safe; *c; ++c) {
    const unsigned char ch = static_cast<unsigned char>(*c);
    if (ch < 0x20 || ch > 0x7e) return {};
  }
  return std::string(safe);
}

bool Rtti::Validate(const CRTTIObject* object) const {
  if (!object || !ready_) return false;
  // A live polymorphic object begins with a readable vtable pointer. This
  // rejects a recycled or freed pointer before it is handed to the engine.
  void* vtable = nullptr;
  if (!Read(object, &vtable) || !vtable) return false;
  void* first_slot = nullptr;
  if (!Read(vtable, &first_slot) || !first_slot) return false;
  return !ClassName(object).empty();
}

bool Rtti::LooksLikeObject(const void* p) const {
  if (!p || !Readable(p, sizeof(void*))) return false;
  void* vtable = nullptr;
  if (!Read(p, &vtable) || !vtable) return false;
  if (!Readable(vtable, sizeof(void*))) return false;
  void* first_slot = nullptr;
  if (!Read(vtable, &first_slot) || !first_slot) return false;
  // A real vtable's entries point at code. This is what rejects a candidate
  // offset that happens to contain a plausible-looking pointer to data.
  return Executable(first_slot, 1);
}

CRTTIObject* Rtti::AsRttiObject(void* interface_pointer) const {
  if (!interface_pointer || !object_class_name_) return nullptr;

  // Do not probe offsets by calling CRTTIObject::GetClassNameA on each one.
  // An IControlObject sub-object has a perfectly valid vtable, but it is not a
  // CRTTIObject. Calling the reflection method on it was the actor-walk crash:
  // the first candidate looked polymorphic, then the call interpreted the
  // wrong interface as reflection state.
  void* vtable_raw = nullptr;
  if (!Read(interface_pointer, &vtable_raw) || !vtable_raw) return nullptr;
  auto** vtable = static_cast<void**>(vtable_raw);

  void* locator_raw = nullptr;
  if (!Read(vtable - 1, &locator_raw) || !locator_raw) return nullptr;

  CompleteObjectLocator64 locator = {};
  if (!Read(locator_raw, &locator) || locator.signature != 1) return nullptr;

  MEMORY_BASIC_INFORMATION memory = {};
  if (!VirtualQuery(locator_raw, &memory, sizeof(memory)) ||
      !memory.AllocationBase) {
    return nullptr;
  }
  auto* image = static_cast<uint8_t*>(memory.AllocationBase);

  IMAGE_DOS_HEADER dos = {};
  if (!Read(image, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew <= 0) {
    return nullptr;
  }
  IMAGE_NT_HEADERS64 nt = {};
  if (!Read(image + dos.e_lfanew, &nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
    return nullptr;
  }
  const uint32_t image_size = nt.OptionalHeader.SizeOfImage;

  // pSelf is part of the x64 COL specifically so the image base can be
  // recovered. Requiring it to agree rejects a random data pointer that merely
  // starts with the signature value one.
  const uintptr_t locator_address = reinterpret_cast<uintptr_t>(locator_raw);
  const uintptr_t image_address = reinterpret_cast<uintptr_t>(image);
  if (locator_address < image_address ||
      locator.self_rva != static_cast<int32_t>(locator_address - image_address)) {
    return nullptr;
  }

  const uintptr_t interface_address =
      reinterpret_cast<uintptr_t>(interface_pointer);
  if (locator.subobject_offset > interface_address) return nullptr;
  auto* complete = reinterpret_cast<uint8_t*>(
      interface_address - locator.subobject_offset);

  const void* hierarchy_raw = nullptr;
  if (!ImageRange(image, image_size, locator.class_descriptor_rva,
                  sizeof(ClassHierarchyDescriptor64), &hierarchy_raw)) {
    return nullptr;
  }
  ClassHierarchyDescriptor64 hierarchy = {};
  if (!Read(hierarchy_raw, &hierarchy) || hierarchy.base_count == 0 ||
      hierarchy.base_count > 256) {
    return nullptr;
  }

  const void* array_raw = nullptr;
  if (!ImageRange(image, image_size, hierarchy.base_array_rva,
                  static_cast<size_t>(hierarchy.base_count) * sizeof(int32_t),
                  &array_raw)) {
    return nullptr;
  }
  const auto* base_rvas = static_cast<const int32_t*>(array_raw);

  constexpr char kReflectionBaseName[] = ".?AVCRTTIObject@@";
  for (uint32_t i = 0; i < hierarchy.base_count; ++i) {
    int32_t descriptor_rva = 0;
    if (!Read(base_rvas + i, &descriptor_rva)) return nullptr;

    const void* descriptor_raw = nullptr;
    if (!ImageRange(image, image_size, descriptor_rva,
                    sizeof(BaseClassDescriptor64), &descriptor_raw)) {
      continue;
    }
    BaseClassDescriptor64 descriptor = {};
    if (!Read(descriptor_raw, &descriptor)) continue;

    // TypeDescriptor begins with two pointers; its decorated class name is the
    // trailing byte array at +16 on x64.
    const void* type_raw = nullptr;
    if (!ImageRange(image, image_size, descriptor.type_descriptor_rva, 17,
                    &type_raw)) {
      continue;
    }
    const char* decorated = SafeCStr(static_cast<const uint8_t*>(type_raw) + 16,
                                     128);
    if (!decorated || std::strcmp(decorated, kReflectionBaseName) != 0) continue;

    // Dead Island's CRTTIObject base is non-virtual (pdisp == -1). Refuse a
    // virtual-base layout instead of guessing through a vbtable.
    if (descriptor.vbtable_displacement != -1 ||
        descriptor.member_displacement < 0 ||
        descriptor.member_displacement > 1024 * 1024) {
      return nullptr;
    }
    auto* candidate = complete + descriptor.member_displacement;
    if (!LooksLikeObject(candidate)) return nullptr;

    // This is now a base whose RTTI record explicitly names CRTTIObject, so the
    // reflection call is type-correct. The result remains sanity-checked.
    const std::string name = ClassName(candidate);
    if (name.empty()) return nullptr;

    static bool logged_adjustment = false;
    if (!logged_adjustment) {
      logged_adjustment = true;
      DIS_INFO("[rtti] converted interface %p to CRTTIObject %p using MSVC RTTI"
               " (interface +%u, reflection +%d)",
               interface_pointer, candidate, locator.subobject_offset,
               descriptor.member_displacement);
    }
    return candidate;
  }
  return nullptr;
}

// ------------------------------------------------------------------- reading
bool Rtti::HasField(const CRTTIObject* object, const char* name) const {
  if (!object || !name || !object_find_field_) return false;
  return reinterpret_cast<FnFindField>(object_find_field_)(object, name) != nullptr;
}

int Rtti::FieldTypeOf(const CRTTIObject* object, const char* name) const {
  if (!object || !name || !object_find_field_ || !field_get_type_) return -1;
  const void* field =
      reinterpret_cast<FnFindField>(object_find_field_)(object, name);
  if (!field) return -1;
  return reinterpret_cast<FnFieldType>(field_get_type_)(field);
}

bool Rtti::ReadFieldRaw(const CRTTIObject* object, const char* name,
                        void* out) const {
  if (!object || !name || !out || !object_get_float_) return false;
  // In this engine build every typed CRTTIObject::GetField* export is folded
  // to one generic routine.  It finds the descriptor and dispatches through
  // that descriptor's value-reader; the exported spelling does NOT enforce
  // the destination type.  Call it only after FieldTypeOf has proved the
  // buffer layout at each public getter below.
  return reinterpret_cast<FnGetRaw>(object_get_float_)(object, name, out) !=
         nullptr;
}

bool Rtti::GetFloat(const CRTTIObject* object, const char* name, float* out) const {
  constexpr int kFloat = 9;
  if (!object || !name || !out || FieldTypeOf(object, name) != kFloat) {
    return false;
  }
  float value = 0.0f;
  if (!ReadFieldRaw(object, name, &value)) return false;
  *out = value;
  return true;
}

bool Rtti::GetInt(const CRTTIObject* object, const char* name, int* out) const {
  if (!object || !name || !out) return false;
  const int type = FieldTypeOf(object, name);
  // ERTTIType values verified from engine_x64_rwdi.dll's
  // GetRTTITypeName table.  Enum values use the same four-byte payload as int.
  if (type < 1 || type > 6) {
    if (type != 20) return false;
  }
  uint64_t raw = 0;
  if (!ReadFieldRaw(object, name, &raw)) return false;
  int8_t s8 = 0;
  uint8_t u8 = 0;
  int16_t s16 = 0;
  uint16_t u16 = 0;
  int32_t s32 = 0;
  uint32_t u32 = 0;
  std::memcpy(&s8, &raw, sizeof(s8));
  std::memcpy(&u8, &raw, sizeof(u8));
  std::memcpy(&s16, &raw, sizeof(s16));
  std::memcpy(&u16, &raw, sizeof(u16));
  std::memcpy(&s32, &raw, sizeof(s32));
  std::memcpy(&u32, &raw, sizeof(u32));
  switch (type) {
    case 1: *out = static_cast<int>(s8); break;
    case 2: *out = static_cast<int>(u8); break;
    case 3: *out = static_cast<int>(s16); break;
    case 4: *out = static_cast<int>(u16); break;
    case 5:
    case 20: *out = s32; break;
    case 6: *out = static_cast<int>(u32); break;
    default: return false;
  }
  return true;
}

bool Rtti::GetBool(const CRTTIObject* object, const char* name, bool* out) const {
  constexpr int kBool = 11;
  if (!object || !name || !out || FieldTypeOf(object, name) != kBool) {
    return false;
  }
  bool value = false;
  if (!ReadFieldRaw(object, name, &value)) return false;
  *out = value;
  return true;
}

bool Rtti::GetString(const CRTTIObject* object, const char* name, std::string* out) const {
  constexpr int kString = 12;
  if (!object || !name || !out || FieldTypeOf(object, name) != kString) {
    return false;
  }
  // Returns ttl::string_base<char> through an out-reference. A 64-byte scratch
  // buffer is more than any small-string layout needs, and the value is read
  // back defensively rather than by assuming where the characters sit.
  alignas(16) uint8_t buffer[64] = {};
  // Strings use a different descriptor virtual than numeric fields (the
  // exported string accessor dispatches through +0x90, not +0xb0).
  if (!object_get_string_ ||
      !reinterpret_cast<FnGetString>(object_get_string_)(object, name, buffer)) {
    return false;
  }
  *out = ReadTtlString(buffer);
  return !out->empty();
}

bool Rtti::GetNumber(const CRTTIObject* object, const char* name, double* out) const {
  if (!object || !name || !out) return false;
  float f = 0.0f;
  if (GetFloat(object, name, &f)) { *out = f; return true; }
  int i = 0;
  if (GetInt(object, name, &i)) { *out = i; return true; }
  constexpr int kInt64 = 7;
  constexpr int kUInt64 = 8;
  constexpr int kDouble = 10;
  const int type = FieldTypeOf(object, name);
  if (type == kInt64) {
    int64_t value = 0;
    if (ReadFieldRaw(object, name, &value)) {
      *out = static_cast<double>(value);
      return true;
    }
  }
  if (type == kUInt64) {
    uint64_t value = 0;
    if (ReadFieldRaw(object, name, &value)) {
      *out = static_cast<double>(value);
      return true;
    }
  }
  if (type == kDouble) {
    double d = 0.0;
    if (ReadFieldRaw(object, name, &d)) {
      *out = d;
      return true;
    }
  }
  bool b = false;
  if (GetBool(object, name, &b)) { *out = b ? 1.0 : 0.0; return true; }
  return false;
}

bool Rtti::GetFirstNumber(const CRTTIObject* object, const char* const* names,
                          int count, double* out, const char** which) const {
  for (int i = 0; i < count; ++i) {
    if (!names[i]) continue;
    if (GetNumber(object, names[i], out)) {
      if (which) *which = names[i];
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------------- writing
bool Rtti::SetFloat(CRTTIObject* object, const char* name, float value) const {
  if (!object || !name || !object_set_float_ || FieldTypeOf(object, name) != 9) {
    return false;
  }
  return reinterpret_cast<FnSetFloat>(object_set_float_)(object, name, value) != nullptr;
}

bool Rtti::SetInt(CRTTIObject* object, const char* name, int value) const {
  if (!object || !name || !object_set_int_) return false;
  const int type = FieldTypeOf(object, name);
  if (type != 5 && type != 20) return false;
  return reinterpret_cast<FnSetInt>(object_set_int_)(object, name, value) != nullptr;
}

bool Rtti::SetBool(CRTTIObject* object, const char* name, bool value) const {
  if (!object || !name || !object_set_bool_ || FieldTypeOf(object, name) != 11) {
    return false;
  }
  return reinterpret_cast<FnSetBool>(object_set_bool_)(object, name, value) != nullptr;
}

// --------------------------------------------------------------- enumeration
std::vector<Rtti::Probe> Rtti::ProbeFields(const CRTTIObject* object,
                                           const char* const* names,
                                           int count) const {
  std::vector<Probe> hits;
  if (!object || !names || !ready_) return hits;
  for (int i = 0; i < count; ++i) {
    const char* name = names[i];
    if (!name || !HasField(object, name)) continue;

    Probe hit;
    hit.name = name;
    char rendered[128] = {};

    float f = 0.0f;
    int n = 0;
    bool b = false;
    std::string s;
    if (GetFloat(object, name, &f)) {
      std::snprintf(rendered, sizeof(rendered), "%.3f", f);
      hit.kind = "float";
    } else if (GetInt(object, name, &n)) {
      std::snprintf(rendered, sizeof(rendered), "%d", n);
      hit.kind = "int";
    } else if (GetBool(object, name, &b)) {
      std::snprintf(rendered, sizeof(rendered), "%s", b ? "true" : "false");
      hit.kind = "bool";
    } else if (GetString(object, name, &s)) {
      std::snprintf(rendered, sizeof(rendered), "%.120s", s.c_str());
      hit.kind = "string";
    } else {
      // The field exists but is a type this mod does not read -- a vector, a
      // pointer, a container. Worth naming in the dump anyway, because it says
      // the structure is there to be reached later.
      std::snprintf(rendered, sizeof(rendered), "<unread>");
      hit.kind = "other";
    }
    hit.value = rendered;
    hits.push_back(hit);
  }
  return hits;
}

// --------------------------------------------------- real field enumeration
const CRTTI* Rtti::NativeClass(const char* mangled_symbol) const {
  if (!g_module || !mangled_symbol) return nullptr;
  using FnNativeClass = const CRTTI* (*)();
  auto fn = reinterpret_cast<FnNativeClass>(
      GetProcAddress(g_module, mangled_symbol));
  return fn ? fn() : nullptr;
}

std::string Rtti::ClassNameOf(const CRTTI* type) const {
  if (!type || !class_get_class_name_ || !Readable(type, 0xa0)) return {};
  // GetClassNameA on CRTTI is UEBA -- virtual -- but CRTTI is a concrete class
  // and the pointer came from a static GetNativeClass, so its dynamic type is
  // exactly CRTTI. The result is still checked for printable ASCII below.
  using FnName = const char* (*)(const CRTTI*);
  const char* raw = reinterpret_cast<FnName>(class_get_class_name_)(type);
  const char* safe = SafeCStr(raw, 128);
  if (!safe || !*safe) return {};
  for (const char* c = safe; *c; ++c) {
    const unsigned char ch = static_cast<unsigned char>(*c);
    if (ch < 0x20 || ch > 0x7e) return {};
  }
  return std::string(safe);
}

bool Rtti::LearnVectorLayout() {
  if (layout_.known()) return true;
  if (!class_get_fields_ || !field_get_name_) return false;

  // Several probe classes, not one.
  //
  // The first attempt used ILevel alone and failed: ILevel is an interface and
  // declares no fields of its own, so GetFields returned an empty vector and
  // there was nothing to validate against. An empty container cannot teach you
  // its layout. These are concrete classes with real field lists, tried in
  // turn until one of them is populated.
  static const char* const kProbes[] = {
      "?GetNativeClass@IUIElement@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IUIText@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IUIImage@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IUIScreen@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IModelObject@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IControlObject@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@ILightObject@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IGSObject@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IBaseCamera@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@IUIProgressText@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@ITerrainObject@@SAPEBVCRTTI@@XZ",
      "?GetNativeClass@ILevel@@SAPEBVCRTTI@@XZ",
  };

  using FnGetFields = const void* (*)(const CRTTI*);
  using FnFieldName = const char* (*)(const CRTTIField*);

  auto field_name_ok = [&](const void* element) {
    if (!element) return false;
    void* vtable = nullptr;
    if (!Read(element, &vtable) || !vtable) return false;
    const char* raw = reinterpret_cast<FnFieldName>(field_get_name_)(element);
    const char* safe = SafeCStr(raw, 128);
    if (!safe || !*safe) return false;
    for (const char* c = safe; *c; ++c) {
      const unsigned char ch = static_cast<unsigned char>(*c);
      if (ch < 0x20 || ch > 0x7e) return false;
    }
    return true;
  };

  const void* sample = nullptr;
  const char* sample_class = nullptr;

  for (const char* symbol : kProbes) {
    const CRTTI* type = NativeClass(symbol);
    if (!type) continue;
    const void* vec = reinterpret_cast<FnGetFields>(class_get_fields_)(type);
    if (!vec) continue;
    if (!sample) { sample = vec; sample_class = symbol; }

    const auto* base = static_cast<const uint8_t*>(vec);
    for (int offset = 0; offset <= 32; offset += 8) {
      void* first = nullptr;
      void* second = nullptr;
      if (!Read(base + offset, &first)) continue;
      if (!Read(base + offset + sizeof(void*), &second)) continue;
      if (!first) continue;

      for (int style = 0; style < 3; ++style) {
        size_t count = 0;
        if (style == 0) {                       // {begin, end}
          const auto a = reinterpret_cast<uintptr_t>(first);
          const auto b = reinterpret_cast<uintptr_t>(second);
          if (b < a) continue;
          const uintptr_t span = b - a;
          if (span % sizeof(void*)) continue;
          count = span / sizeof(void*);
        } else if (style == 1) {                // {data, size_t}
          count = reinterpret_cast<size_t>(second);
        } else {                                // {data, uint32}
          count = static_cast<size_t>(reinterpret_cast<uintptr_t>(second) & 0xffffffffu);
        }
        if (count == 0 || count > 4096) continue;

        const size_t probes[3] = {0, count / 2, count - 1};
        bool all_ok = true;
        for (size_t k : probes) {
          void* element = nullptr;
          if (!Read(static_cast<const uint8_t*>(first) + k * sizeof(void*),
                    &element) || !field_name_ok(element)) {
            all_ok = false;
            break;
          }
        }
        if (!all_ok) continue;

        layout_.data_offset = offset;
        layout_.end_pointer = (style == 0);
        DIS_INFO("[rtti] ttl::vector layout learned from %s:"
                 " data at +%d, %s, %d fields",
                 symbol, offset,
                 layout_.end_pointer ? "end-pointer" : "explicit size",
                 static_cast<int>(count));
        return true;
      }
    }
  }

  // Failed. Print the raw words of one container so the shape can be worked
  // out offline instead of by another round of guessing on the device.
  DIS_ERR("[rtti] could not work out the ttl::vector layout");
  if (sample) {
    DIS_ERR("[rtti]   sample from %s at %p:", sample_class, sample);
    for (int i = 0; i < 6; ++i) {
      void* word = nullptr;
      if (Read(static_cast<const uint8_t*>(sample) + i * sizeof(void*), &word)) {
        DIS_ERR("[rtti]     +%02d = %p", i * 8, word);
      } else {
        DIS_ERR("[rtti]     +%02d = <unreadable>", i * 8);
      }
    }
  } else {
    DIS_ERR("[rtti]   no probe class returned a container at all");
  }
  return false;
}

std::vector<FieldInfo> Rtti::Fields(const CRTTI* type) const {
  std::vector<FieldInfo> out;
  if (!type || !layout_.known() || !class_get_fields_ || !field_get_name_) {
    return out;
  }
  using FnGetFields = const void* (*)(const CRTTI*);
  const void* vec = reinterpret_cast<FnGetFields>(class_get_fields_)(type);
  if (!vec) return out;

  const auto* base = static_cast<const uint8_t*>(vec) + layout_.data_offset;
  void* first = nullptr;
  void* second = nullptr;
  if (!Read(base, &first) || !Read(base + sizeof(void*), &second)) return out;
  if (!first) return out;

  size_t count = 0;
  if (layout_.end_pointer) {
    const auto a = reinterpret_cast<uintptr_t>(first);
    const auto b = reinterpret_cast<uintptr_t>(second);
    if (b < a) return out;
    count = (b - a) / sizeof(void*);
  } else {
    // Same 32-bit size / 32-bit capacity packing as ReadVector documents.
    const auto word = reinterpret_cast<uintptr_t>(second);
    count = static_cast<size_t>(word);
    if (count > 4096) count = static_cast<size_t>(word & 0xffffffffu);
  }
  if (count > 4096) return out;

  using FnFieldName = const char* (*)(const CRTTIField*);
  using FnFieldType = int (*)(const CRTTIField*);
  for (size_t i = 0; i < count; ++i) {
    void* element = nullptr;
    if (!Read(static_cast<const uint8_t*>(first) + i * sizeof(void*),
              &element) || !element) {
      continue;
    }
    void* vtable = nullptr;
    if (!Read(element, &vtable) || !vtable) continue;

    const char* raw = reinterpret_cast<FnFieldName>(field_get_name_)(element);
    const char* safe = SafeCStr(raw, 128);
    if (!safe || !*safe) continue;

    FieldInfo info;
    info.name = safe;
    if (field_get_type_) {
      info.type.raw = reinterpret_cast<FnFieldType>(field_get_type_)(element);
    }
    out.push_back(info);
  }
  return out;
}

bool Rtti::ReadVector(const void* container, std::vector<void*>* out,
                      const char* what) const {
  if (!container || !out || !layout_.known()) return false;
  out->clear();

  const auto* base = static_cast<const uint8_t*>(container) + layout_.data_offset;
  void* first = nullptr;
  void* second = nullptr;
  if (!Read(base, &first) || !Read(base + sizeof(void*), &second)) {
    DIS_ERR("[rtti] %s: container is unreadable", what);
    return false;
  }

  // Size and capacity are two 32-bit fields sharing one 64-bit word.
  //
  // This was learned the hard way. Reading all 64 bits of the second word gave
  // 2,478,196,130,369 for a real container, which is (577 << 32) | 577 -- the
  // same number twice, because size and capacity were both 577. The layout
  // probe never caught it because CRTTI::GetFields happened to return a vector
  // whose upper half was zero, so a 64-bit read and a 32-bit read agreed.
  //
  // So neither width is assumed. Both are tried and whichever produces element
  // pointers that actually read as objects wins, which is the same
  // self-validating rule the layout probe uses.
  auto plausible = [&](size_t count) {
    if (!first || count == 0 || count > 262144) return false;
    const size_t probes[3] = {0, count / 2, count - 1};
    for (size_t k : probes) {
      void* element = nullptr;
      if (!Read(static_cast<const uint8_t*>(first) + k * sizeof(void*),
                &element) || !element) {
        return false;
      }
      void* vtable = nullptr;
      if (!Read(element, &vtable) || !vtable) return false;
    }
    return true;
  };

  size_t count = 0;
  if (layout_.end_pointer) {
    const auto a = reinterpret_cast<uintptr_t>(first);
    const auto b = reinterpret_cast<uintptr_t>(second);
    count = (b >= a) ? (b - a) / sizeof(void*) : 0;
    if (!plausible(count)) count = 0;
  } else {
    const auto word = reinterpret_cast<uintptr_t>(second);
    const size_t wide = static_cast<size_t>(word);
    const size_t narrow = static_cast<size_t>(word & 0xffffffffu);
    if (plausible(wide)) {
      count = wide;
    } else if (plausible(narrow)) {
      count = narrow;
      DIS_INFO("[rtti] %s: size is a 32-bit field (%llu); the upper half is"
               " capacity", what, static_cast<unsigned long long>(narrow));
    }
  }

  if (count == 0) {
    DIS_ERR("[rtti] %s: nothing usable -- data=%p word=%llu", what, first,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(second)));
    return false;
  }

  out->reserve(count < 16384 ? count : 16384);
  for (size_t i = 0; i < count && out->size() < 16384; ++i) {
    void* element = nullptr;
    if (Read(static_cast<const uint8_t*>(first) + i * sizeof(void*), &element) &&
        element) {
      out->push_back(element);
    }
  }
  DIS_INFO("[rtti] %s: %llu entries, %d readable", what,
           static_cast<unsigned long long>(count),
           static_cast<int>(out->size()));
  return !out->empty();
}

bool Rtti::EnumerateObjects(CRTTIObject* seed, std::vector<CRTTIObject*>* out) const {
  if (!out || !seed || !object_get_all_ || !layout_.known()) return false;
  if (!Validate(seed)) return false;
  out->clear();

  // Bigger than any three-word container needs, zeroed, and aligned. The
  // engine sees an empty vector and allocates for itself.
  alignas(16) uint8_t container[64] = {};

  using FnGetAll = void (*)(CRTTIObject*, void*, bool);
  reinterpret_cast<FnGetAll>(object_get_all_)(seed, container, true);

  const uint8_t* base = container + layout_.data_offset;
  void* first = nullptr;
  void* second = nullptr;
  if (!Read(base, &first) || !Read(base + sizeof(void*), &second)) return false;
  if (!first) return false;

  size_t count = 0;
  if (layout_.end_pointer) {
    const auto a = reinterpret_cast<uintptr_t>(first);
    const auto b = reinterpret_cast<uintptr_t>(second);
    if (b < a) return false;
    count = (b - a) / sizeof(void*);
  } else {
    // This engine packs uint32 size and uint32 capacity into the word after
    // the data pointer. Treating the whole 64-bit word as size turns any
    // nonzero capacity into an absurd count.
    count = static_cast<size_t>(
        reinterpret_cast<uintptr_t>(second) & 0xffffffffu);
  }
  // A level holds thousands of objects; a million means the read is wrong.
  if (count == 0 || count > 262144) {
    DIS_ERR("[rtti] GetAllObjects returned an implausible count (%llu)",
            static_cast<unsigned long long>(count));
    return false;
  }

  out->reserve(count < 8192 ? count : 8192);
  for (size_t i = 0; i < count && out->size() < 8192; ++i) {
    void* element = nullptr;
    if (!Read(static_cast<const uint8_t*>(first) + i * sizeof(void*),
              &element) || !element) {
      continue;
    }
    out->push_back(element);
  }
  DIS_INFO("[rtti] GetAllObjects returned %llu objects (%d usable)",
           static_cast<unsigned long long>(count),
           static_cast<int>(out->size()));
  return !out->empty();
}

const CRTTI* Rtti::ClassOf(const CRTTIObject* object) const {
  if (!object || !class_get_class_name_) return nullptr;
  const std::string expected = ClassName(object);
  if (expected.empty()) return nullptr;   // not a CRTTIObject at all

  void* vtable_raw = nullptr;
  if (!Read(object, &vtable_raw) || !vtable_raw) return nullptr;
  auto* vtable = static_cast<void**>(vtable_raw);

  using FnGetRtti = const CRTTI* (*)(const CRTTIObject*);

  // A game class can expose two related descriptors. GetRTTI is normally the
  // gameplay leaf (PlayerDI), while GetEngineRTTI may intentionally return an
  // engine-side base. The old code tried only the latter and then rejected it
  // because its name did not match PlayerDI, preventing enumeration entirely.
  // Try both *known* adjacent virtual slots and accept only an exact class-name
  // match; this is still fail-closed and never scans or guesses a vtable slot.
  const int slots[2] = {rtti_slot_, engine_rtti_slot_};
  for (int slot : slots) {
    if (slot < 0) continue;
    void* entry = nullptr;
    if (!Read(vtable + slot, &entry) || !entry || !Executable(entry, 1)) {
      continue;
    }
    const CRTTI* type = reinterpret_cast<FnGetRtti>(entry)(object);
    if (!type || !Readable(type, 0xa0)) continue;
    const std::string actual = ClassNameOf(type);
    if (actual == expected) return type;
    DIS_INFO("[rtti] class slot %d reported '%s' while object reports '%s'",
             slot, actual.empty() ? "<invalid>" : actual.c_str(),
             expected.c_str());
  }
  DIS_WARN("[rtti] neither RTTI slot produced the live class '%s'",
           expected.c_str());
  return nullptr;
}

std::vector<Rtti::Probe> Rtti::DumpAllFields(const CRTTIObject* object) const {
  std::vector<Probe> out;
  const CRTTI* type = ClassOf(object);
  if (!type) return out;

  // GetFields() is deliberately a direct-field accessor (the engine function
  // is just `lea rax,[rcx+0xc8]`).  Gameplay classes such as PlayerDI declare
  // almost everything on reflected bases, so asking only the leaf class can
  // quite correctly return an empty vector even though FindField() resolves
  // inherited names such as m_Health.
  //
  // Walk the engine's own non-virtual GetBaseClass API rather than assuming
  // the CRTTI layout here.  Passing the current type as the second parameter
  // is required for game-DLL classes: the exported implementation uses that
  // argument's +0xc0 bridge when the normal engine base pointer is absent.
  std::vector<FieldInfo> fields;
  std::unordered_set<std::string> seen_names;
  std::unordered_set<const CRTTI*> seen_types;
  const CRTTI* current = type;
  for (int depth = 0; current && depth < 64; ++depth) {
    if (!seen_types.insert(current).second) break;
    const std::string class_name = ClassNameOf(current);
    if (class_name.empty()) break;

    const std::vector<FieldInfo> direct = Fields(current);
    DIS_INFO("[rtti] field walk depth %d: %s has %d direct fields", depth,
             class_name.c_str(), static_cast<int>(direct.size()));
    for (const FieldInfo& field : direct) {
      // Derived declarations win, matching CRTTIObject::FindField.
      if (seen_names.insert(field.name).second) fields.push_back(field);
    }

    if (!class_get_base_) break;
    const CRTTI* next = reinterpret_cast<FnGetBase>(class_get_base_)(current,
                                                                     current);
    if (!next || !Readable(next, 0xa0)) break;
    current = next;
  }

  out.reserve(fields.size());
  for (const FieldInfo& field : fields) {
    Probe hit;
    hit.name = field.name;
    char rendered[128] = {};

    // This function is a class-shape diagnostic, not a bulk serializer. Some
    // reflected PlayerDI descriptors have custom readers with gameplay
    // preconditions; invoking every one during save entry crashed immediately
    // after a successful 313-field hierarchy walk. Enumerating descriptor
    // names/types is safe and is all discovery needs. Known scalar candidates
    // are sampled separately through ProbeFields/GetNumber.
    std::snprintf(rendered, sizeof(rendered), "<type %d>", field.type.raw);
    hit.kind = "field";
    hit.value = rendered;
    out.push_back(hit);
  }
  return out;
}

}  // namespace dis
