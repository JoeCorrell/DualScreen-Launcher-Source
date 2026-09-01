// ChromeEngine's reflection system, which is how this mod reads the game.
//
// Dead Island splits itself across two modules and they could not be less alike
// from the outside. `engine_x64_rwdi.dll` exports 6,502 named C++ symbols;
// `gamedll_x64_rwdi.dll`, which holds every gameplay system this mod cares
// about -- the player, its vitals, its inventory, its skills -- exports exactly
// two, and neither is useful:
//
//     InitializeGameScriptDLL
//     ShutdownGameScriptDLL
//
// So there is no export to call for the player's health, and the obvious
// alternative -- find the field's byte offset and read it -- is the thing this
// mod refuses to do, because an offset is a number that is right until the game
// is patched and then silently reads something else.
//
// The seam is that gamedll's classes are not opaque. They register themselves
// into the *engine's* reflection system, and that system is fully exported:
//
//     CRTTIObject::GetClassNameA()                  what this object is
//     CRTTIObject::FindField(name)                  does it have that field
//     CRTTIObject::GetFieldFloat(name, out)         read it, by name
//     CRTTIObject::SetFieldFloat(name, value)       write it, by name
//     CRTTI::GetFields()                            enumerate every field
//     CRTTIField::GetName() / GetType()             what each one is called
//
// That is the whole basis of this mod's data layer: every value on the second
// screen is fetched by *name* through an exported engine function. No offsets,
// no signatures, no disassembly. A game patch that moves a field does not break
// it; only one that renames a field would, and that shows up as a named miss in
// the log rather than a plausible wrong number.
//
// One detail matters enough to state plainly, because getting it wrong is what
// the previous attempt did. The GetField* family is declared `QEBA` in its
// mangled name -- a *non-virtual* public const member. It is therefore correct
// to call the exported address directly. The earlier code instead used
// `CDynamicRTTIObject::GetFloat`, which is `UEBA`, a virtual: calling that
// export directly runs the base-class implementation and bypasses whatever the
// real object overrides it with, which is why every vital it read came back
// 0.00. Non-virtual exports are called directly; virtuals are dispatched
// through the object's own vtable, never through the export.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dis {

// Opaque engine types. Never dereferenced: every access goes through an
// exported function, which is the entire point of this file.
using CRTTIObject = void;
using CRTTI = void;
using CRTTIField = void;

// CRTTIField::GetType returns ERTTIType. The numeric values are not published
// anywhere, so this mod does not depend on them -- the discovery dump prints
// the raw value and the typed getters are tried in turn instead. Kept as a
// distinct type so a raw enum value is never mistaken for a field's contents.
struct FieldType {
  int raw = -1;
};

struct FieldInfo {
  std::string name;
  FieldType type;
};

// One resolved export, kept as a record so the log can name what was missing
// on a build that differs rather than failing as a null call.
struct RttiSymbol {
  const char* mangled;
  const char* pretty;
  void* address = nullptr;
};

class Rtti {
 public:
  // Resolves every symbol below out of the already-mapped engine module.
  bool Resolve(void* engine_module);

  bool ready() const { return ready_; }
  const std::vector<RttiSymbol>& symbols() const { return symbols_; }
  int missing() const;

  // ---- identity -------------------------------------------------------
  // The class name of any CRTTIObject. This is the primitive the player search
  // is built on: enumerate candidate objects, ask each what it is.
  std::string ClassName(const CRTTIObject* object) const;

  // True when `object` looks like a live CRTTIObject at all. Reads the vtable
  // pointer and the class name defensively, so a stale or recycled pointer
  // answers false instead of taking the process down.
  bool Validate(const CRTTIObject* object) const;

  // Finds the CRTTIObject base inside an object reached through some other
  // interface pointer, or null when there isn't one.
  //
  // This exists because of a mistake made twice in one session. A ChromeEngine
  // actor inherits several interfaces, so an `IControlObject*` from
  // FindObjectsInRadius and an `ILevel*` from the UI-manager hook both point at
  // sub-objects that are NOT the CRTTIObject base -- on CLevel the gap was
  // measured at 16 bytes. Handing such a pointer to GetClassNameA reads the
  // wrong words: the first time it returned garbage, the second time it took
  // the process down mid-loop over 577 actors.
  //
  // So the base is recovered from the object's MSVC complete-object locator
  // and class hierarchy. The hierarchy must explicitly name CRTTIObject before
  // GetClassNameA is called. Merely finding a readable vtable is not enough:
  // IControlObject also has one, and calling reflection through that valid but
  // wrong interface was the original actor-walk crash.
  CRTTIObject* AsRttiObject(void* interface_pointer) const;

  // The screening test on its own: does this address hold something that looks
  // like a live polymorphic object?
  bool LooksLikeObject(const void* p) const;

  // ---- reading, by name ----------------------------------------------
  // Each returns false when the field does not exist on this class, which is
  // how a page distinguishes "not supported by this build" from "zero".
  bool HasField(const CRTTIObject* object, const char* name) const;
  bool GetFloat(const CRTTIObject* object, const char* name, float* out) const;
  bool GetInt(const CRTTIObject* object, const char* name, int* out) const;
  bool GetBool(const CRTTIObject* object, const char* name, bool* out) const;
  bool GetString(const CRTTIObject* object, const char* name, std::string* out) const;

  // Reads whichever of the numeric types the field actually is, so a caller
  // that just wants a number does not have to know whether the game stored it
  // as a float or an int. Used by the HUD for values like Level and Money that
  // differ in type between builds.
  bool GetNumber(const CRTTIObject* object, const char* name, double* out) const;

  // First name that exists on the object wins. The catalog in GameState.cpp
  // lists several spellings per value because the discovery dump is what
  // settles which one this build uses.
  bool GetFirstNumber(const CRTTIObject* object, const char* const* names,
                      int count, double* out, const char** which = nullptr) const;

  // ---- writing, by name ----------------------------------------------
  // Only ever used behind an explicit user action on the panel.
  bool SetFloat(CRTTIObject* object, const char* name, float value) const;
  bool SetInt(CRTTIObject* object, const char* name, int value) const;
  bool SetBool(CRTTIObject* object, const char* name, bool value) const;

  // ---- enumeration ----------------------------------------------------
  // Which of `names` this object actually declares, with each hit's current
  // value. This is what makes the mod self-describing: run it once against the
  // live game and the log names the real fields instead of this source
  // guessing at them.
  //
  // It probes a dictionary rather than walking CRTTI::GetFields, and that is a
  // deliberate trade. Reaching a CRTTI* means calling GetEngineRTTI, which is
  // virtual, so it would have to be dispatched through a vtable slot found by
  // scanning -- calling unidentified slots on a live object to see what happens
  // is the same move that had to be reverted from the Deus Ex mod. FindField is
  // non-virtual, exported, and answers the question directly. The dictionary
  // costs nothing to be wrong about: a name that does not exist is one failed
  // string compare inside the engine.
  struct Probe {
    std::string name;
    std::string value;   // rendered, whichever type answered
    const char* kind = "";
  };
  std::vector<Probe> ProbeFields(const CRTTIObject* object,
                                 const char* const* names, int count) const;

  // ---- real enumeration -----------------------------------------------
  // How a ttl::vector is laid out in memory, worked out from a live one
  // rather than assumed.
  //
  // This is the primitive the dictionary above was a substitute for.
  // `CRTTI::GetFields` returns `const ttl::vector<const CRTTIField*>&`, and
  // once the three words of that structure can be read, every field of a class
  // can be listed by name -- no candidate list, no guessing which spellings
  // this build uses.
  //
  // It is learned, not hard-coded, and it is safe to learn because every call
  // involved is either static or non-virtual and the answer checks itself:
  // `ILevel::GetNativeClass()` is `SA`, so it needs no object and cannot be the
  // wrong sub-object; the candidate layout is only accepted if the pointers it
  // yields are CRTTIFields whose `GetName()` reads back as printable ASCII.
  // A wrong guess fails that test and is discarded rather than dereferenced
  // further.
  struct VectorLayout {
    int data_offset = -1;    // bytes to the element pointer
    bool end_pointer = false;// second word is end-pointer, not a count
    bool known() const { return data_offset >= 0; }
  };

  bool LearnVectorLayout();
  const VectorLayout& vector_layout() const { return layout_; }

  // A class handle for one of the 43 engine classes that export a static
  // GetNativeClass. `symbol` is the full mangled name.
  const CRTTI* NativeClass(const char* mangled_symbol) const;

  // Every field a class declares, by name. Empty until the layout is learned.
  std::vector<FieldInfo> Fields(const CRTTI* type) const;
  std::string ClassNameOf(const CRTTI* type) const;

  // Every live object the engine knows about.
  //
  // `CRTTIObject::GetAllObjects` fills a ttl::vector, which is why it was out
  // of reach until the layout above was learned: there was no way to hand it a
  // container it would accept, and no way to read back what it wrote. It is
  // the enumeration the id sweep was standing in for -- and the id sweep found
  // one object in 65,536 tries, so it was standing in badly.
  //
  // Two cautions, both load-bearing:
  //
  // GetAllObjects is mangled UEAA -- virtual -- so this runs CRTTIObject's own
  // implementation rather than any override. That is the intent here (the base
  // walks the engine-wide registry), but it means `seed` must genuinely be a
  // CRTTIObject, which the caller establishes with Validate/ClassName first.
  //
  // The container is handed over zeroed, which is what a default-constructed
  // vector looks like, so the engine allocates its own storage. That storage is
  // never freed: this runs once per level, and calling an allocator whose
  // layout is inferred is a worse risk than leaking a few kilobytes.
  //
  // Answers false when the layout is unknown, so it fails closed.
  bool EnumerateObjects(CRTTIObject* seed, std::vector<CRTTIObject*>* out) const;

  // The class of a live object, including gamedll classes that have no static
  // accessor.
  //
  // This needs `CRTTIObject::GetEngineRTTI`, which is virtual, and the rule
  // everywhere else in this file is that virtuals are never called at their
  // exported address. The rule is kept. The slot is recovered once from
  // CRTTIObject's exported base vtable instead of from the live object: a
  // gameplay class such as PlayerDI overrides GetEngineRTTI, so its vtable
  // correctly contains a different function address and cannot be searched
  // for the base export.
  //
  // Once the index is known it is correct for every class, because a vtable
  // slot index is fixed across a hierarchy -- a subclass that overrides
  // GetEngineRTTI puts its override in the same slot. So calling through the
  // slot is ordinary virtual dispatch, not a guess.
  //
  // The result is still validated: the returned CRTTI must report a class name
  // matching the object's own GetClassNameA before it is handed back.
  const CRTTI* ClassOf(const CRTTIObject* object) const;

  // Reads a ttl::vector the engine filled, using the learned layout.
  // `container` is the raw bytes handed to the engine as an out-parameter.
  // Logs what it saw when it cannot make sense of it, because a silent false
  // here is indistinguishable from "the game returned nothing".
  bool ReadVector(const void* container, std::vector<void*>* out,
                  const char* what) const;

  // Every field a live object declares, by name and with its current value.
  // This is what ProbeFields was approximating, done properly.
  std::vector<Probe> DumpAllFields(const CRTTIObject* object) const;

 private:
  void* Find(const char* mangled, const char* pretty);
  int FieldTypeOf(const CRTTIObject* object, const char* name) const;
  bool ReadFieldRaw(const CRTTIObject* object, const char* name,
                    void* out) const;

  bool ready_ = false;
  std::vector<RttiSymbol> symbols_;

  void* object_class_name_ = nullptr;
  void* object_find_field_ = nullptr;
  void* object_get_float_ = nullptr;
  void* object_get_int_ = nullptr;
  void* object_get_bool_ = nullptr;
  void* object_get_string_ = nullptr;
  void* object_get_double_ = nullptr;
  void* object_get_enum_ = nullptr;
  void* object_set_float_ = nullptr;
  void* object_set_int_ = nullptr;
  void* object_set_bool_ = nullptr;
  void* class_get_fields_ = nullptr;
  void* class_get_base_ = nullptr;
  void* class_find_field_ = nullptr;
  void* field_get_name_ = nullptr;
  void* field_get_type_ = nullptr;
  void* class_get_class_name_ = nullptr;
  void* object_get_all_ = nullptr;
  void* object_get_rtti_ = nullptr;
  void* object_get_engine_rtti_ = nullptr;
  void* object_vtable_ = nullptr;
  int rtti_slot_ = -1;
  int engine_rtti_slot_ = -1;
  VectorLayout layout_;
};

Rtti& TheRtti();

}  // namespace dis
