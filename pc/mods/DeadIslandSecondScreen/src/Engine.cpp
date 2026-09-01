#include "Engine.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "Log.h"
#include "Mem.h"

namespace dis {
namespace {

Engine g_engine;

// The exact exported names, taken from the DLL's export table rather than
// written from memory. A typo here would be a null pointer at run time and a
// silent do-nothing; taking them from the binary is why Resolve can report
// which ones were actually found.
//
// Signatures, decoded from the mangling:
//
//   ?GetIUIManager@ILevel@@QEAAPEAVIUIManager@@XZ
//       IUIManager* ILevel::GetIUIManager()
//   ?GetUIScreen@ILevel@@QEAAPEAVIUIElement@@H_N@Z
//       IUIElement* ILevel::GetUIScreen(int id, bool)
//   ?GetPos@IUIElement@@QEBA?AVvec3@@XZ
//       vec3 IUIElement::GetPos() const          -- 12 bytes, hidden return ptr
//   ?SetPos@IUIElement@@QEAAXAEBVvec3@@@Z
//       void IUIElement::SetPos(const vec3&)
//
// GetVisibleUIScreens is deliberately not used: it takes a
// ttl::listm<77, IUIScreen*, ttl::allocator>& whose layout would have to be
// reverse-engineered, and GetUIScreen(id) reaches every screen without it.
constexpr const char kLevelGetUIManager[] =
    "?GetIUIManager@ILevel@@QEAAPEAVIUIManager@@XZ";
constexpr const char kLevelGetUIScreen[] =
    "?GetUIScreen@ILevel@@QEAAPEAVIUIElement@@H_N@Z";
constexpr const char kScreenGetScriptName[] =
    "?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ";
constexpr const char kScreenGetId[] = "?GetScreenId@IUIScreen@@QEBAHXZ";
constexpr const char kScreenShow[] = "?Show@IUIScreen@@UEAAXPEAX@Z";
constexpr const char kScreenHide[] = "?Hide@IUIScreen@@UEAAXXZ";
constexpr const char kScreenSetLayer[] = "?SetLayer@IUIScreen@@QEAAXH@Z";
constexpr const char kElementGetPos[] = "?GetPos@IUIElement@@QEBA?AVvec3@@XZ";
constexpr const char kElementGetRelativePos[] =
    "?GetRelativePos@IUIElement@@QEBA?AVvec3@@XZ";
constexpr const char kElementSetPos[] = "?SetPos@IUIElement@@QEAAXAEBVvec3@@@Z";
constexpr const char kElementGetScale[] = "?GetScale@IUIElement@@QEBAAEBVvec3@@XZ";
constexpr const char kElementSetScale[] = "?SetScale@IUIElement@@QEAAXAEBVvec3@@@Z";
constexpr const char kElementGetSizeX[] = "?GetSizeX@IUIElement@@QEBAMXZ";
constexpr const char kElementGetSizeY[] = "?GetSizeY@IUIElement@@QEBAMXZ";
constexpr const char kElementGetUnscaledSizeX[] =
    "?GetUnscaledSizeX@IUIElement@@QEBAMXZ";
constexpr const char kElementGetUnscaledSizeY[] =
    "?GetUnscaledSizeY@IUIElement@@QEBAMXZ";
constexpr const char kElementIsVisible[] = "?IsVisible@IUIElement@@QEBA_NXZ";
constexpr const char kElementSetVisible[] = "?SetVisible@IUIElement@@QEAAX_N@Z";
constexpr const char kElementSetOpacity[] = "?SetOpacity@IUIElement@@QEAAXM@Z";
constexpr const char kElementChildCount[] = "?GetChildrenCount@IUIElement@@QEAAHXZ";
constexpr const char kElementGetChildren[] = "?GetChildren@IUIElement@@QEAAPEAV1@H@Z";
constexpr const char kLevelIsRegistered[] = "?IsUIScreenRegistered@ILevel@@QEAA_NH@Z";
constexpr const char kLevelIsLoaded[] = "?IsUIScreenLoaded@ILevel@@QEAA_NH@Z";
constexpr const char kLevelIsVisible[] = "?IsUIScreenVisible@ILevel@@QEAA_NH@Z";
constexpr const char kLevelFindScreen[] = "?FindUIScreen@ILevel@@QEAAPEAVIUIElement@@H@Z";
constexpr const char kLevelUIManagerVisible[] = "?IsUIManagerVisible@ILevel@@QEBA_NXZ";
constexpr const char kLevelIsLoading[] = "?IsLoading@ILevel@@QEBA_NXZ";
constexpr const char kLevelScreenIdByName[] =
    "?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z";
constexpr const char kLevelLoadScreen[] = "?LoadUIScreen@ILevel@@QEAAPEAVIUIScreen@@H@Z";
constexpr const char kLevelShowScreen[] = "?ShowUIScreen@ILevel@@QEAAXHPEAVIUIData@@@Z";
constexpr const char kLevelHideScreen[] = "?HideUIScreen@ILevel@@QEAAXH@Z";
constexpr const char kControlWorldPosition[] =
    "?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ";
constexpr const char kControlForwardVector[] =
    "?GetForwardVector@IControlObject@@QEBA?AVvec3@@XZ";
constexpr const char kLevelTerrainSize[] = "?GetTerrainSize@ILevel@@QEAA?AVvec3@@XZ";
constexpr const char kLevelName[] = "?GetLevelName@ILevel@@QEAAPEBDXZ";
constexpr const char kLevelGetVarSet[] = "?GetVarSet@ILevel@@UEAAPEAVCVarSet@@XZ";
constexpr const char kObjectGetFloat[] = "?GetFloat@CDynamicRTTIObject@@UEBAMPEBD@Z";
constexpr const char kObjectGetInt[] = "?GetInt@CDynamicRTTIObject@@UEBAHPEBD@Z";
constexpr const char kTextGetText[] =
    "?GetText@IUIText@@UEAAAEBV?$string_base@D@ttl@@XZ";
constexpr const char kImageGetTextureName[] =
    "?GetTextureName@IUIImage@@QEAAAEBV?$string_base@D@ttl@@XZ";
constexpr const char kElementFindByPath[] =
    "?FindElementByPath@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@@Z";
constexpr const char kElementFind[] =
    "?FindElement@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@_N@Z";
constexpr const char kControlGetText[] =
    "?GetText@IUIElementControl@@UEAAAEBV?$string_base@D@ttl@@H@Z";
constexpr const char kControlSelect[] = "?Select@IUIElementControl@@QEAAXXZ";
constexpr const char kControlClick[] = "?OnClickLB@IUIElementControl@@UEAA_NMM@Z";
constexpr const char kControlIsSelected[] = "?IsSelected@IUIElementControl@@QEAA_NXZ";
constexpr const char kControlIsEnabled[] = "?IsEnabled@IUIElementControl@@QEAA_NXZ";
constexpr const char kControlVtable[] = "??_7IUIElementControl@@6B@";
constexpr const char kControlTypeName[] = ".?AVIUIElementControl@@";
constexpr const char kTextTypeName[] = ".?AVIUIText@@";
constexpr const char kImageTypeName[] = ".?AVIUIImage@@";

// MinGW-w64 on x86_64 uses the Microsoft ABI, so an exported member function is
// callable as a plain function with `this` as the first argument. Nothing here
// needs a thunk, which is the other reason this game is so much less work than
// a 32-bit __thiscall target.
using FnGetUIManager = IUIManager* (*)(ILevel*);
using FnGetUIScreen = IUIElement* (*)(ILevel*, int, bool);
using FnScreenName = const void* (*)(IUIScreen*);
using FnScreenId = int (*)(IUIScreen*);
using FnScreenShow = void (*)(IUIScreen*, void*);
using FnScreenHide = void (*)(IUIScreen*);
using FnScreenSetLayer = void (*)(IUIScreen*, int);
// A 12-byte class returned by an MSVC x64 *member* keeps `this` in RCX and
// places the hidden result pointer in RDX. Reversing these two corrupts the
// object and was the cause of seemingly unrelated UI/map crashes.
using FnGetElementVec3 = Vec3* (*)(IUIElement*, Vec3*);
using FnGetControlVec3 = Vec3* (*)(const IControlObject*, Vec3*);
using FnGetLevelVec3 = Vec3* (*)(ILevel*, Vec3*);
using FnGetVec3Ref = const Vec3* (*)(IUIElement*);
using FnSetVec3 = void (*)(IUIElement*, const Vec3*);
using FnGetFloat = float (*)(IUIElement*);
using FnGetBool = bool (*)(IUIElement*);
using FnSetBool = void (*)(IUIElement*, bool);
using FnSetFloat = void (*)(IUIElement*, float);
using FnGetInt = int (*)(IUIElement*);
using FnGetChild = IUIElement* (*)(IUIElement*, int);
using FnLevelIntBool = bool (*)(ILevel*, int);
using FnLevelIntElem = IUIElement* (*)(ILevel*, int);
using FnLevelBool = bool (*)(ILevel*);
using FnLevelNameId = int (*)(ILevel*, const void*);
using FnLevelLoad = IUIScreen* (*)(ILevel*, int);
using FnLevelShowId = void (*)(ILevel*, int, void*);
using FnLevelHideId = void (*)(ILevel*, int);
using FnLevelName = const char* (*)(ILevel*);
using FnLevelVarSet = void* (*)(ILevel*);
using FnObjectFloat = float (*)(void*, const char*);
using FnObjectInt = int (*)(void*, const char*);
using FnFindElementByPath = IUIElement* (*)(IUIElement*, const void*);
using FnFindElement = IUIElement* (*)(IUIElement*, const void*, bool);
using FnControlText = const void* (*)(IUIElement*, int);
using FnControlSelect = void (*)(IUIElement*);
using FnControlBool = bool (*)(IUIElement*);
using FnControlClick = bool (*)(IUIElement*, float, float);

struct TtlStringView {
  const char* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};
static_assert(sizeof(TtlStringView) == 16, "ttl::string_base view ABI changed");

TtlStringView ViewOf(const char* text) {
  TtlStringView view;
  if (!text) return view;
  const size_t length = std::strlen(text);
  if (length > UINT32_MAX) return view;
  view.data = text;
  view.size = static_cast<uint32_t>(length);
  view.capacity = view.size;
  return view;
}

HMODULE g_engine_dll = nullptr;

// The Definitive Edition shipped with one engine binary on the supported PC
// and Android/Wine builds.  Its exported abstract-interface vtables are not
// address points for the concrete controls, so scanning those exports cannot
// discover three late virtuals used by the menu adapter.  Keep the fallback
// behind the PE identity and the exported function signatures: a different
// engine must fail closed instead of calling a guessed slot.
bool IsSupportedUiVtableBuild(HMODULE module) {
  if (!module) return false;
  const auto* base = reinterpret_cast<const uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      base + static_cast<std::size_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      nt->FileHeader.TimeDateStamp != 0x5745b4fau ||
      nt->OptionalHeader.SizeOfImage != 0x00b8a000u) {
    return false;
  }

  struct Signature {
    const char* export_name;
    const uint8_t* bytes;
    std::size_t size;
  };
  static constexpr uint8_t kControlText[] = {
      0x48, 0x8b, 0x49, 0x18, 0x48, 0x85, 0xc9, 0x74, 0x05};
  static constexpr uint8_t kControlClickBytes[] = {
      0xc7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, 0x32, 0xc0, 0xc3};
  static constexpr uint8_t kTextText[] = {
      0x48, 0x8b, 0x41, 0x18, 0x48, 0x85, 0xc0, 0x74, 0x07,
      0x48, 0x05, 0xd8, 0x03, 0x00, 0x00, 0xc3};
  const Signature signatures[] = {
      {kControlGetText, kControlText, sizeof(kControlText)},
      {kControlClick, kControlClickBytes, sizeof(kControlClickBytes)},
      {kTextGetText, kTextText, sizeof(kTextText)},
  };
  for (const Signature& signature : signatures) {
    const void* address = reinterpret_cast<const void*>(
        GetProcAddress(module, signature.export_name));
    if (!address || std::memcmp(address, signature.bytes, signature.size) != 0) {
      return false;
    }
  }
  return true;
}

// The MSVC x64 RTTI records referenced by vtable[-1]. Every pointer-like field
// in these records is an image-relative 32-bit RVA, including in a 64-bit
// executable. The layouts are part of the compiler ABI and are independently
// exercised by Rtti::AsRttiObject for gameplay objects.
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

// Proves that a live polymorphic object implements a particular engine UI
// interface before any interface-specific virtual is dispatched. This is not
// a class-name heuristic: the answer comes from the complete object's own
// compiler-emitted base-class hierarchy. Any malformed, stale, non-image, or
// unfamiliar record fails closed.
bool MsvcObjectIsA(const void* object, const char* decorated_base_name) {
  if (!object || !decorated_base_name || !*decorated_base_name) return false;

  void* vtable_raw = nullptr;
  if (!Read(object, &vtable_raw) || !vtable_raw) return false;
  auto** vtable = static_cast<void**>(vtable_raw);
  void* first_slot = nullptr;
  if (!Read(vtable, &first_slot) || !first_slot ||
      !Executable(first_slot, 1)) {
    return false;
  }

  void* locator_raw = nullptr;
  if (!Read(vtable - 1, &locator_raw) || !locator_raw) return false;
  CompleteObjectLocator64 locator = {};
  if (!Read(locator_raw, &locator) || locator.signature != 1 ||
      locator.self_rva <= 0) {
    return false;
  }

  MEMORY_BASIC_INFORMATION memory = {};
  if (!VirtualQuery(locator_raw, &memory, sizeof(memory)) ||
      !memory.AllocationBase) {
    return false;
  }
  auto* image = static_cast<uint8_t*>(memory.AllocationBase);

  IMAGE_DOS_HEADER dos = {};
  if (!Read(image, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew <= 0) {
    return false;
  }
  IMAGE_NT_HEADERS64 nt = {};
  if (!Read(image + dos.e_lfanew, &nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      nt.OptionalHeader.SizeOfImage == 0) {
    return false;
  }
  const uint32_t image_size = nt.OptionalHeader.SizeOfImage;

  // pSelf is specific to the x64 form and makes a strong check that the
  // alleged locator really belongs to this image rather than merely beginning
  // with the signature value one.
  const uintptr_t locator_address = reinterpret_cast<uintptr_t>(locator_raw);
  const uintptr_t image_address = reinterpret_cast<uintptr_t>(image);
  if (locator_address < image_address ||
      static_cast<uint64_t>(locator_address - image_address) >= image_size ||
      locator.self_rva !=
          static_cast<int32_t>(locator_address - image_address)) {
    return false;
  }

  const void* hierarchy_raw = nullptr;
  if (!ImageRange(image, image_size, locator.class_descriptor_rva,
                  sizeof(ClassHierarchyDescriptor64), &hierarchy_raw)) {
    return false;
  }
  ClassHierarchyDescriptor64 hierarchy = {};
  if (!Read(hierarchy_raw, &hierarchy) || hierarchy.base_count == 0 ||
      hierarchy.base_count > 256) {
    return false;
  }

  const void* array_raw = nullptr;
  if (!ImageRange(image, image_size, hierarchy.base_array_rva,
                  static_cast<size_t>(hierarchy.base_count) * sizeof(int32_t),
                  &array_raw)) {
    return false;
  }
  const auto* base_rvas = static_cast<const int32_t*>(array_raw);
  for (uint32_t i = 0; i < hierarchy.base_count; ++i) {
    int32_t descriptor_rva = 0;
    if (!Read(base_rvas + i, &descriptor_rva)) return false;

    const void* descriptor_raw = nullptr;
    if (!ImageRange(image, image_size, descriptor_rva,
                    sizeof(BaseClassDescriptor64), &descriptor_raw)) {
      return false;
    }
    BaseClassDescriptor64 descriptor = {};
    if (!Read(descriptor_raw, &descriptor)) return false;

    // TypeDescriptor is two pointers followed by its decorated name on x64.
    const void* type_raw = nullptr;
    if (!ImageRange(image, image_size, descriptor.type_descriptor_rva,
                    2 * sizeof(void*) + 1, &type_raw)) {
      return false;
    }
    const char* decorated = SafeCStr(
        static_cast<const uint8_t*>(type_raw) + 2 * sizeof(void*), 128);
    if (decorated && std::strcmp(decorated, decorated_base_name) == 0) {
      // Proving that the complete object contains the interface is not enough
      // for a member call: `object` must point at that exact base sub-object.
      // Refuse virtual bases rather than guessing through a vbtable.
      const uintptr_t object_address = reinterpret_cast<uintptr_t>(object);
      if (locator.subobject_offset > object_address ||
          descriptor.vbtable_displacement != -1 ||
          descriptor.member_displacement < 0) {
        return false;
      }
      const uintptr_t complete_address =
          object_address - locator.subobject_offset;
      const uintptr_t member_displacement =
          static_cast<uint32_t>(descriptor.member_displacement);
      if (complete_address > UINTPTR_MAX - member_displacement) return false;
      const uintptr_t target_address = complete_address + member_displacement;
      return target_address == object_address;
    }
  }
  return false;
}

// Verified against ILevel::GetUIScreenIdByScriptName in the shipped engine:
// ttl::string_base<char> is {data pointer, uint32 size, uint32 capacity} here.
// Copy the owned length rather than searching for a NUL; this preserves long,
// multiline and localized strings and never reads beyond the native object.
std::string ReadTtlString(const void* s) {
  if (!s) return {};
  TtlStringView view;
  if (!Read(s, &view)) return {};
  constexpr uint32_t kMaximumUiTextBytes = 4096;
  if (view.size == 0) return {};
  if (!view.data || view.size > kMaximumUiTextBytes ||
      view.capacity < view.size || !Readable(view.data, view.size)) {
    return {};
  }
  std::string value(view.data, view.data + view.size);
  const std::size_t nul = value.find('\0');
  if (nul != std::string::npos) value.resize(nul);
  return value;
}

}  // namespace

Engine& TheEngine() { return g_engine; }

void* Engine::Find(const char* mangled, const char* pretty) {
  void* address = g_engine_dll
      ? reinterpret_cast<void*>(GetProcAddress(g_engine_dll, mangled)) : nullptr;
  symbols_.push_back(Symbol{mangled, pretty, address});
  return address;
}

int Engine::missing() const {
  int n = 0;
  for (const Symbol& s : symbols_) if (!s.address) ++n;
  return n;
}

bool Engine::Resolve() {
  if (ready_) return true;
  // Already mapped -- the game imports it -- so this is a reference, not a load.
  g_engine_dll = GetModuleHandleW(L"engine_x64_rwdi.dll");
  if (!g_engine_dll) {
    DIS_ERR("[eng] engine_x64_rwdi.dll is not loaded yet");
    return false;
  }
  symbols_.clear();

  level_get_ui_manager_    = Find(kLevelGetUIManager,   "ILevel::GetIUIManager");
  level_get_visible_screens_ = nullptr;  // deliberately unused, see the header
  level_get_loaded_screens_  = nullptr;
  screen_get_script_name_  = Find(kScreenGetScriptName, "IUIScreen::GetScriptName");
  screen_get_id_           = Find(kScreenGetId,         "IUIScreen::GetScreenId");
  screen_show_             = Find(kScreenShow,          "IUIScreen::Show");
  screen_hide_             = Find(kScreenHide,          "IUIScreen::Hide");
  screen_set_layer_        = Find(kScreenSetLayer,      "IUIScreen::SetLayer");
  // GetDebugId returns an engine-owned string class by value. Its constructor,
  // destructor and storage size are not part of the exported ABI, so the old
  // guessed scratch-buffer call is intentionally not resolved or invoked.
  element_get_pos_         = Find(kElementGetPos,       "IUIElement::GetPos");
  element_get_relative_pos_= Find(kElementGetRelativePos,
                                  "IUIElement::GetRelativePos");
  element_set_pos_         = Find(kElementSetPos,       "IUIElement::SetPos");
  element_get_scale_       = Find(kElementGetScale,     "IUIElement::GetScale");
  element_set_scale_       = Find(kElementSetScale,     "IUIElement::SetScale");
  element_get_size_x_      = Find(kElementGetSizeX,     "IUIElement::GetSizeX");
  element_get_size_y_      = Find(kElementGetSizeY,     "IUIElement::GetSizeY");
  element_get_unscaled_size_x_ = Find(kElementGetUnscaledSizeX,
                                      "IUIElement::GetUnscaledSizeX");
  element_get_unscaled_size_y_ = Find(kElementGetUnscaledSizeY,
                                      "IUIElement::GetUnscaledSizeY");
  element_is_visible_      = Find(kElementIsVisible,    "IUIElement::IsVisible");
  element_set_visible_     = Find(kElementSetVisible,   "IUIElement::SetVisible");
  element_set_opacity_     = Find(kElementSetOpacity,   "IUIElement::SetOpacity");
  element_children_count_  = Find(kElementChildCount,   "IUIElement::GetChildrenCount");
  element_get_children_    = Find(kElementGetChildren,  "IUIElement::GetChildren");
  // Resolved separately because it is reached from an element, not the level.
  level_get_loaded_screens_ = Find(kLevelGetUIScreen,   "ILevel::GetUIScreen");
  level_is_registered_     = Find(kLevelIsRegistered,   "ILevel::IsUIScreenRegistered");
  level_is_loaded_         = Find(kLevelIsLoaded,       "ILevel::IsUIScreenLoaded");
  level_is_visible_        = Find(kLevelIsVisible,      "ILevel::IsUIScreenVisible");
  level_find_screen_       = Find(kLevelFindScreen,     "ILevel::FindUIScreen");
  level_ui_manager_visible_= Find(kLevelUIManagerVisible, "ILevel::IsUIManagerVisible");
  // Optional by design.  Do not add this to symbols_ through Find(): missing()
  // contains required UI ABI only, while IsLoading merely replaces the legacy
  // conservative timer when the engine supplies it.
  level_is_loading_ = reinterpret_cast<void*>(
      GetProcAddress(g_engine_dll, kLevelIsLoading));
  if (level_is_loading_) {
    DIS_INFO("[eng] optional ILevel::IsLoading readiness signal resolved");
  } else {
    DIS_WARN("[eng] ILevel::IsLoading is unavailable; player discovery will "
             "use the conservative fallback delay");
  }
  level_screen_id_by_name_ = Find(kLevelScreenIdByName,
                                  "ILevel::GetUIScreenIdByScriptName");
  level_load_screen_       = Find(kLevelLoadScreen,     "ILevel::LoadUIScreen");
  level_show_screen_       = Find(kLevelShowScreen,     "ILevel::ShowUIScreen");
  level_hide_screen_       = Find(kLevelHideScreen,     "ILevel::HideUIScreen");
  level_world_position_    = Find(kControlWorldPosition,
                                  "IControlObject::GetWorldPosition");
  level_forward_vector_    = Find(kControlForwardVector,
                                  "IControlObject::GetForwardVector");
  level_terrain_size_      = Find(kLevelTerrainSize,    "ILevel::GetTerrainSize");
  level_name_              = Find(kLevelName,           "ILevel::GetLevelName");
  level_get_var_set_       = Find(kLevelGetVarSet,      "ILevel::GetVarSet");
  object_get_float_        = Find(kObjectGetFloat,      "CDynamicRTTIObject::GetFloat");
  object_get_int_          = Find(kObjectGetInt,        "CDynamicRTTIObject::GetInt");
  text_get_text_           = Find(kTextGetText,         "IUIText::GetText");
  image_get_texture_name_  = Find(kImageGetTextureName,
                                  "IUIImage::GetTextureName");
  element_find_by_path_    = Find(kElementFindByPath,   "IUIElement::FindElementByPath");
  element_find_            = Find(kElementFind,         "IUIElement::FindElement");
  control_get_text_        = Find(kControlGetText,      "IUIElementControl::GetText");
  control_select_          = Find(kControlSelect,       "IUIElementControl::Select");
  control_click_           = Find(kControlClick,        "IUIElementControl::OnClickLB");
  control_is_selected_     = Find(kControlIsSelected,   "IUIElementControl::IsSelected");
  control_is_enabled_      = Find(kControlIsEnabled,    "IUIElementControl::IsEnabled");

  // GetText is virtual. Derive its slot from the exported base vtable instead
  // of freezing the current 0x5b0 offset into the mod. Derived controls put
  // their override in that same slot, so ordinary virtual dispatch then reads
  // the actual row/button text.
  control_text_slot_ = -1;
  control_click_slot_ = -1;
  auto** control_vtable = reinterpret_cast<void**>(
      GetProcAddress(g_engine_dll, kControlVtable));
  if (control_vtable && control_get_text_) {
    for (int slot = 0; slot < 320; ++slot) {
      void* candidate = nullptr;
      if (!Read(control_vtable + slot, &candidate)) break;
      if (candidate == control_get_text_) {
        control_text_slot_ = slot;
        DIS_INFO("[eng] IUIElementControl::GetText virtual slot = %d (0x%x)",
                 slot, slot * static_cast<int>(sizeof(void*)));
      }
      if (candidate == control_click_) {
        control_click_slot_ = slot;
        DIS_INFO("[eng] IUIElementControl::OnClickLB virtual slot = %d (0x%x)",
                 slot, slot * static_cast<int>(sizeof(void*)));
      }
      if (control_text_slot_ >= 0 && control_click_slot_ >= 0) break;
    }
  }
  if ((control_text_slot_ < 0 || control_click_slot_ < 0) &&
      IsSupportedUiVtableBuild(g_engine_dll)) {
    // Concrete address points in engine_x64_rwdi.dll 2016-05-25:
    // IUIElementControl +0x5c0 GetText, +0x760 OnClickLB. Runtime calls still
    // validate the object's MSVC hierarchy and require the resolved target to
    // be executable before dispatching. IUIText uses its verified direct
    // export instead of a guessed concrete-vtable slot.
    control_text_slot_ = 0x5c0 / static_cast<int>(sizeof(void*));
    control_click_slot_ = 0x760 / static_cast<int>(sizeof(void*));
    DIS_INFO("[eng] verified Definitive Edition UI slots: control text=%d, "
             "click=%d", control_text_slot_, control_click_slot_);
  }
  if (control_text_slot_ < 0) {
    DIS_WARN("[eng] could not derive IUIElementControl::GetText slot; native"
             " menu labels will fail closed");
  }
  if (control_click_slot_ < 0) {
    DIS_WARN("[eng] could not derive IUIElementControl::OnClickLB slot; native"
             " menu actions will fail closed");
  }

  DIS_INFO("[eng] engine_x64_rwdi.dll at %p, %d symbols resolved, %d missing",
           static_cast<void*>(g_engine_dll),
           static_cast<int>(symbols_.size()) - missing(), missing());
  for (const Symbol& s : symbols_) {
    if (!s.address) DIS_ERR("[eng]   MISSING %s", s.pretty);
  }
  ready_ = missing() == 0;
  return ready_;
}

// ---------------------------------------------------------------- ILevel
IUIManager* Engine::GetUIManager(ILevel* level) const {
  if (!level || !level_get_ui_manager_) return nullptr;
  return reinterpret_cast<FnGetUIManager>(level_get_ui_manager_)(level);
}

bool Engine::GetVisibleScreens(ILevel*, std::vector<IUIScreen*>*) const {
  return false;  // see the header: GetUIScreen(id) is used instead
}

bool Engine::IsUIManagerVisible(ILevel* level) const {
  return (level && level_ui_manager_visible_)
      ? reinterpret_cast<FnLevelBool>(level_ui_manager_visible_)(level) : false;
}

bool Engine::IsLoading(ILevel* level) const {
  // A missing optional export deliberately answers true. Callers that want the
  // legacy fallback distinguish that case with HasLoadingState(); no caller may
  // accidentally treat an unknown engine as ready.
  return (level && level_is_loading_)
      ? reinterpret_cast<FnLevelBool>(level_is_loading_)(level) : true;
}

bool Engine::IsScreenRegistered(ILevel* level, int id) const {
  return (level && level_is_registered_)
      ? reinterpret_cast<FnLevelIntBool>(level_is_registered_)(level, id) : false;
}

bool Engine::IsScreenLoaded(ILevel* level, int id) const {
  return (level && level_is_loaded_)
      ? reinterpret_cast<FnLevelIntBool>(level_is_loaded_)(level, id) : false;
}

bool Engine::IsScreenVisible(ILevel* level, int id) const {
  return (level && level_is_visible_)
      ? reinterpret_cast<FnLevelIntBool>(level_is_visible_)(level, id) : false;
}

IUIElement* Engine::FindScreen(ILevel* level, int id) const {
  return (level && level_find_screen_)
      ? reinterpret_cast<FnLevelIntElem>(level_find_screen_)(level, id) : nullptr;
}

int Engine::ScreenIdByScriptName(ILevel* level, const char* script_name) const {
  if (!level || !script_name || !*script_name || !level_screen_id_by_name_ ||
      !IsUIManagerVisible(level)) {
    return -1;
  }
  const TtlStringView name = ViewOf(script_name);
  return reinterpret_cast<FnLevelNameId>(level_screen_id_by_name_)(level, &name);
}

IUIScreen* Engine::LoadScreen(ILevel* level, int id) const {
  if (!level || id < 0 || !level_load_screen_ || !IsUIManagerVisible(level)) {
    return nullptr;
  }
  return reinterpret_cast<FnLevelLoad>(level_load_screen_)(level, id);
}

void Engine::ShowScreenById(ILevel* level, int id) const {
  if (level && id >= 0 && level_show_screen_ && IsUIManagerVisible(level)) {
    reinterpret_cast<FnLevelShowId>(level_show_screen_)(level, id, nullptr);
  }
}

void Engine::HideScreenById(ILevel* level, int id) const {
  if (level && id >= 0 && level_hide_screen_ && IsUIManagerVisible(level)) {
    reinterpret_cast<FnLevelHideId>(level_hide_screen_)(level, id);
  }
}

bool Engine::WorldPosition(const IControlObject* object, Vec3* out) const {
  if (!object || !out || !level_world_position_ ||
      !Readable(object, sizeof(void*))) return false;
  Vec3 value;
  reinterpret_cast<FnGetControlVec3>(level_world_position_)(object, &value);
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z)) return false;
  *out = value;
  return true;
}

bool Engine::ForwardVector(const IControlObject* object, Vec3* out) const {
  if (!object || !out || !level_forward_vector_ ||
      !Readable(object, sizeof(void*))) return false;
  Vec3 value;
  reinterpret_cast<FnGetControlVec3>(level_forward_vector_)(object, &value);
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z)) return false;
  *out = value;
  return true;
}

bool Engine::TerrainSize(ILevel* level, Vec3* out) const {
  if (!level || !out || !level_terrain_size_ || !IsUIManagerVisible(level)) {
    return false;
  }
  Vec3 value;
  reinterpret_cast<FnGetLevelVec3>(level_terrain_size_)(level, &value);
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z)) return false;
  *out = value;
  return value.x > 0.0f || value.z > 0.0f;
}

std::string Engine::LevelName(ILevel* level) const {
  if (!level || !level_name_) return {};
  const char* name = reinterpret_cast<FnLevelName>(level_name_)(level);
  const char* safe = SafeCStr(name, 256);
  return safe ? std::string(safe) : std::string();
}

// The first attempt assumed screen ids were a dense 0..255 range and passed
// them to GetUIScreen(id, false). It returned nothing for all 256, which was
// the assumption failing rather than the game having no UI: the engine exports
// GetUIScreenIdByScriptName, so ids come from names and need not be dense.
//
// Asking IsUIScreenRegistered over a wide range costs one integer call per id
// and involves no engine container type, so the range can be widened until it
// stops finding anything rather than guessed at.
bool Engine::GetLoadedScreens(ILevel* level, std::vector<IUIScreen*>* out, int max_id) const {
  if (!level || !out) return false;

  // The gate. Without it this walked 4096 ids three times over, from the
  // present hook, while the game was still loading -- roughly twelve thousand
  // calls into a UI manager that does not exist yet, repeated every 120 frames
  // until it succeeded. That is what turned loading into a crash, and it was a
  // change made without asking whether the engine was ready to be asked.
  if (!IsUIManagerVisible(level)) return false;

  // 4096, not 256. The HUD screen is id 1024 -- measured, in an earlier run --
  // so a 256 bound excludes the one screen this mod exists to move. Narrowing
  // the range after the GetDebugId crash was fixing the wrong thing: the scan
  // was never what crashed, and shrinking it only hid the screens.
  //
  // One predicate per id rather than three. IsUIScreenRegistered is the
  // cheapest and the broadest -- loaded and visible are both subsets of it.
  for (int id = 0; id < max_id; ++id) {
    if (!IsScreenRegistered(level, id)) continue;
    if (IUIElement* e = FindScreen(level, id)) out->push_back(e);
  }
  return !out->empty();
}

void* Engine::GetVarSet(ILevel* level) const {
  return (level && level_get_var_set_)
      ? reinterpret_cast<FnLevelVarSet>(level_get_var_set_)(level) : nullptr;
}

float Engine::GetFloat(void* object, const char* name) const {
  if (!object || !name) return 0.0f;
  void* vt = nullptr;
  if (!Read(object, &vt)) return 0.0f;
  void** vtable = static_cast<void**>(vt);
  void* fn = nullptr;
  // CDynamicRTTIObject::GetFloat is vtable slot 4 in this build. Calling the
  // exported base function directly would bypass CVarSet's override and always
  // read zero; the slot must be dispatched through the object's real vtable.
  constexpr uint32_t kGetFloatSlot = 4;
  if (!Read(vtable + kGetFloatSlot, &fn) || !fn) return 0.0f;
  return reinterpret_cast<FnObjectFloat>(fn)(object, name);
}

int Engine::GetInt(void* object, const char* name) const {
  return (object && object_get_int_ && name)
      ? reinterpret_cast<FnObjectInt>(object_get_int_)(object, name) : 0;
}

// ---------------------------------------------------------------- IUIScreen
std::string Engine::ScreenName(IUIScreen* screen) const {
  if (!screen || !screen_get_script_name_) return {};
  const void* s = reinterpret_cast<FnScreenName>(screen_get_script_name_)(screen);
  return ReadTtlString(s);
}

int Engine::ScreenId(IUIScreen* screen) const {
  if (!screen || !screen_get_id_) return -1;
  return reinterpret_cast<FnScreenId>(screen_get_id_)(screen);
}

void Engine::ShowScreen(IUIScreen* screen) const {
  if (screen && screen_show_) reinterpret_cast<FnScreenShow>(screen_show_)(screen, nullptr);
}

void Engine::HideScreen(IUIScreen* screen) const {
  if (screen && screen_hide_) reinterpret_cast<FnScreenHide>(screen_hide_)(screen);
}

void Engine::SetScreenLayer(IUIScreen* screen, int layer) const {
  if (screen && screen_set_layer_) {
    reinterpret_cast<FnScreenSetLayer>(screen_set_layer_)(screen, layer);
  }
}

// ---------------------------------------------------------------- IUIElement
std::string Engine::ElementId(IUIElement* element) const {
  (void)element;
  // Deliberately disabled. GetDebugId returns ttl::string_base<char> by value;
  // without the exact class layout and destructor, even a correctly ordered
  // hidden-return call can corrupt the stack or leak engine storage.
  return {};
}

Vec3 Engine::GetPos(IUIElement* element) const {
  Vec3 out;
  if (element && element_get_pos_) {
    reinterpret_cast<FnGetElementVec3>(element_get_pos_)(element, &out);
  }
  return out;
}

Vec3 Engine::GetRelativePos(IUIElement* element) const {
  Vec3 out;
  if (element && element_get_relative_pos_) {
    reinterpret_cast<FnGetElementVec3>(element_get_relative_pos_)(element,
                                                                   &out);
  }
  return out;
}

void Engine::SetPos(IUIElement* element, const Vec3& p) const {
  if (element && element_set_pos_) {
    reinterpret_cast<FnSetVec3>(element_set_pos_)(element, &p);
  }
}

Vec3 Engine::GetScale(IUIElement* element) const {
  Vec3 out{1, 1, 1};
  if (element && element_get_scale_) {
    const Vec3* s = reinterpret_cast<FnGetVec3Ref>(element_get_scale_)(element);
    if (s && Read(s, &out)) return out;
  }
  return out;
}

void Engine::SetScale(IUIElement* element, const Vec3& s) const {
  if (element && element_set_scale_) {
    reinterpret_cast<FnSetVec3>(element_set_scale_)(element, &s);
  }
}

float Engine::GetSizeX(IUIElement* element) const {
  return (element && element_get_size_x_)
      ? reinterpret_cast<FnGetFloat>(element_get_size_x_)(element) : 0.0f;
}

float Engine::GetSizeY(IUIElement* element) const {
  return (element && element_get_size_y_)
      ? reinterpret_cast<FnGetFloat>(element_get_size_y_)(element) : 0.0f;
}

float Engine::GetUnscaledSizeX(IUIElement* element) const {
  return (element && element_get_unscaled_size_x_)
      ? reinterpret_cast<FnGetFloat>(element_get_unscaled_size_x_)(element)
      : 0.0f;
}

float Engine::GetUnscaledSizeY(IUIElement* element) const {
  return (element && element_get_unscaled_size_y_)
      ? reinterpret_cast<FnGetFloat>(element_get_unscaled_size_y_)(element)
      : 0.0f;
}

bool Engine::IsVisible(IUIElement* element) const {
  return (element && element_is_visible_)
      ? reinterpret_cast<FnGetBool>(element_is_visible_)(element) : false;
}

void Engine::SetVisible(IUIElement* element, bool visible) const {
  if (element && element_set_visible_) {
    reinterpret_cast<FnSetBool>(element_set_visible_)(element, visible);
  }
}

int Engine::ChildCount(IUIElement* element) const {
  return (element && element_children_count_)
      ? reinterpret_cast<FnGetInt>(element_children_count_)(element) : 0;
}

std::string Engine::ElementText(IUIElement* element) const {
  if (!element || !text_get_text_ || !Executable(text_get_text_, 1) ||
      !MsvcObjectIsA(element, kTextTypeName)) {
    return {};
  }
  using FnGetText = const void* (*)(IUIElement*);
  const void* s = reinterpret_cast<FnGetText>(text_get_text_)(element);
  return ReadTtlString(s);
}

void Engine::SetOpacity(IUIElement* element, float opacity) const {
  if (!element || !element_set_opacity_ || !std::isfinite(opacity)) return;
  if (opacity < 0.0f) opacity = 0.0f;
  if (opacity > 1.0f) opacity = 1.0f;
  reinterpret_cast<FnSetFloat>(element_set_opacity_)(element, opacity);
}

IUIElement* Engine::Child(IUIElement* element, int index) const {
  return (element && element_get_children_)
      ? reinterpret_cast<FnGetChild>(element_get_children_)(element, index) : nullptr;
}

IUIElement* Engine::FindElementByPath(IUIElement* root, const char* path) const {
  if (!root || !path || !*path || !element_find_by_path_) return nullptr;
  const auto find = reinterpret_cast<FnFindElementByPath>(element_find_by_path_);
  const TtlStringView value = ViewOf(path);
  if (IUIElement* found = find(root, &value)) return found;

  // Shipped Dead Island XUI navigation paths are authored with Windows
  // separators (for example, G_Content\G_Group\L_List).  Most of the custom
  // bridge uses the easier-to-read forward-slash spelling.  Older engine
  // builds happened to accept both; Definitive Edition does not consistently
  // do so.  Retry only that exact separator-normalized path and retain the
  // original lookup first for compatibility with modded screens.
  std::string authored(path);
  const bool changed = authored.find('/') != std::string::npos;
  if (!changed) return nullptr;
  std::replace(authored.begin(), authored.end(), '/', '\\');
  const TtlStringView authored_value = ViewOf(authored.c_str());
  return find(root, &authored_value);
}

IUIElement* Engine::FindElement(IUIElement* root, const char* id,
                                bool recursive) const {
  if (!root || !id || !*id || !element_find_) return nullptr;
  const TtlStringView value = ViewOf(id);
  return reinterpret_cast<FnFindElement>(element_find_)(root, &value, recursive);
}

bool Engine::IsUiControl(IUIElement* element) const {
  return MsvcObjectIsA(element, kControlTypeName);
}

bool Engine::IsUiText(IUIElement* element) const {
  return MsvcObjectIsA(element, kTextTypeName);
}

bool Engine::IsUiImage(IUIElement* element) const {
  return MsvcObjectIsA(element, kImageTypeName);
}

std::string Engine::ImageTextureName(IUIElement* element) const {
  if (!element || !image_get_texture_name_ ||
      !MsvcObjectIsA(element, kImageTypeName)) {
    return {};
  }
  using FnGetTextureName = const void* (*)(IUIElement*);
  const void* value =
      reinterpret_cast<FnGetTextureName>(image_get_texture_name_)(element);
  return ReadTtlString(value);
}

std::string Engine::ControlText(IUIElement* control, int index) const {
  if (!control || control_text_slot_ < 0 || index < 0 ||
      !MsvcObjectIsA(control, kControlTypeName)) {
    return {};
  }
  void* raw_vtable = nullptr;
  if (!Read(control, &raw_vtable) || !raw_vtable) return {};
  void* target = nullptr;
  if (!Read(static_cast<void**>(raw_vtable) + control_text_slot_, &target) ||
      !target || !Executable(target, 1)) {
    return {};
  }
  const void* text = reinterpret_cast<FnControlText>(target)(control, index);
  return ReadTtlString(text);
}

bool Engine::SelectControl(IUIElement* control) const {
  if (!control || !control_select_ ||
      !MsvcObjectIsA(control, kControlTypeName)) {
    return false;
  }
  reinterpret_cast<FnControlSelect>(control_select_)(control);
  return true;
}

bool Engine::ClickControl(IUIElement* control) const {
  if (!control || control_click_slot_ < 0 ||
      !MsvcObjectIsA(control, kControlTypeName)) {
    return false;
  }
  void* raw_vtable = nullptr;
  if (!Read(control, &raw_vtable) || !raw_vtable) return false;
  void* target = nullptr;
  if (!Read(static_cast<void**>(raw_vtable) + control_click_slot_, &target) ||
      !target || !Executable(target, 1)) {
    return false;
  }
  const float x = GetSizeX(control) > 0.0f ? GetSizeX(control) * 0.5f : 1.0f;
  const float y = GetSizeY(control) > 0.0f ? GetSizeY(control) * 0.5f : 1.0f;
  return reinterpret_cast<FnControlClick>(target)(control, x, y);
}

bool Engine::IsControlSelected(IUIElement* control) const {
  return control && control_is_selected_ &&
             MsvcObjectIsA(control, kControlTypeName)
      ? reinterpret_cast<FnControlBool>(control_is_selected_)(control) : false;
}

bool Engine::IsControlEnabled(IUIElement* control) const {
  return control && control_is_enabled_ &&
             MsvcObjectIsA(control, kControlTypeName)
      ? reinterpret_cast<FnControlBool>(control_is_enabled_)(control) : false;
}

}  // namespace dis
