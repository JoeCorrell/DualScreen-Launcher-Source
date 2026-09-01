// Dead Island's engine, reached the way it was meant to be.
//
// This is the part that makes this game a different proposition from the other
// two mods in this folder. `engine_x64_rwdi.dll` exports 6,502 named C++
// symbols, and among them is a complete public UI object model:
//
//   ILevel::GetIUIManager        the manager
//   ILevel::GetVisibleUIScreens  what is on screen right now
//   ILevel::GetUIScreen          a screen by id
//   IUIScreen::GetScriptName     a screen's name -- identity, not a guess
//   IUIElement::SetPos/SetScale/SetSize/SetVisible
//   IUIScreen::SetScreenWorldMatrix
//
// Every function this mod calls is resolved by name through GetProcAddress,
// which means a game patch that moves code does not break it. Virtual slots are
// derived from exported base vtables, and interface-specific calls are guarded
// by the object's compiler-emitted MSVC RTTI hierarchy. A removed export or an
// unfamiliar object therefore fails closed instead of becoming a wrong call.
//
// The one thing not exported is a global accessor for the level itself, so the
// ILevel instance has to be captured rather than looked up. Resolve() records
// which symbols were found; Probe reports them so a missing one is a line in
// the log rather than a mystery.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dis {

// Opaque engine pointers. Never dereferenced here -- every access goes through
// an exported function, which is the whole point.
using ILevel = void;
using IUIManager = void;
using IUIScreen = void;
using IUIElement = void;
using IControlObject = void;

struct Vec3 { float x = 0, y = 0, z = 0; };

// One resolved export. Kept as a record rather than a bare pointer so the log
// can say exactly which of them were missing on a build that differs.
struct Symbol {
  const char* mangled;
  const char* pretty;
  void* address = nullptr;
};

class Engine {
 public:
  // Loads engine_x64_rwdi.dll (already mapped by the game) and resolves every
  // symbol below. Answers false only when the module itself is absent.
  bool Resolve();

  bool ready() const { return ready_; }
  const std::vector<Symbol>& symbols() const { return symbols_; }
  int missing() const;

  // ---- ILevel ---------------------------------------------------------
  IUIManager* GetUIManager(ILevel* level) const;
  // Fills `out` with the screens currently visible. The engine owns them.
  bool GetVisibleScreens(ILevel* level, std::vector<IUIScreen*>* out) const;
  bool GetLoadedScreens(ILevel* level, std::vector<IUIScreen*>* out,
                        int max_id = 4096) const;

  // Screen ids are not a dense 0..255 range -- the engine exports
  // GetUIScreenIdByScriptName, so they are derived from names. Rather than
  // guess the range, ask: these are all plain (ILevel*, int) calls with no
  // engine container types involved, which is what makes a wide scan cheap.
  // Safe to call at any time, and the gate everything else waits behind: the
  // UI manager does not exist during loading, and asking it about screens
  // before it does is how a load turns into a crash.
  bool IsUIManagerVisible(ILevel* level) const;

  // Definitive Edition exports a cheap, non-virtual loading predicate.  It is
  // optional so a future engine build can retain the conservative discovery
  // delay instead of making the entire UI resolver fail because one new
  // safety signal is absent.
  bool HasLoadingState() const { return level_is_loading_ != nullptr; }
  bool IsLoading(ILevel* level) const;

  bool IsScreenRegistered(ILevel* level, int id) const;
  bool IsScreenLoaded(ILevel* level, int id) const;
  bool IsScreenVisible(ILevel* level, int id) const;
  IUIElement* FindScreen(ILevel* level, int id) const;
  int ScreenIdByScriptName(ILevel* level, const char* script_name) const;
  IUIScreen* LoadScreen(ILevel* level, int id) const;
  void ShowScreenById(ILevel* level, int id) const;
  void HideScreenById(ILevel* level, int id) const;

  // Read-only world values used by the map page. Twelve-byte vectors use the
  // Microsoft x64 member-function ABI: RCX is `this`, RDX is the hidden return
  // buffer. Keeping the calls here prevents that easy-to-reverse detail from
  // leaking into page code.
  bool WorldPosition(const IControlObject* object, Vec3* out) const;
  bool ForwardVector(const IControlObject* object, Vec3* out) const;
  bool TerrainSize(ILevel* level, Vec3* out) const;
  std::string LevelName(ILevel* level) const;

  // Live game-state access through the level's variable set. The names are
  // looked up dynamically, so the custom HUD does not need hard-coded offsets.
  void* GetVarSet(ILevel* level) const;
  float GetFloat(void* object, const char* name) const;
  int GetInt(void* object, const char* name) const;

  // ---- IUIScreen ------------------------------------------------------
  std::string ScreenName(IUIScreen* screen) const;
  int ScreenId(IUIScreen* screen) const;
  void ShowScreen(IUIScreen* screen) const;
  void HideScreen(IUIScreen* screen) const;
  void SetScreenLayer(IUIScreen* screen, int layer) const;

  // ---- IUIElement -----------------------------------------------------
  // Intentionally returns empty. GetDebugId returns ttl::string_base by value,
  // whose construction/destruction ABI is not known; invoking it with a
  // guessed scratch buffer previously corrupted the render thread's stack.
  std::string ElementId(IUIElement* element) const;
  Vec3 GetPos(IUIElement* element) const;
  // Local/authored coordinates. HudRadar writes its generated route nodes via
  // SetRelativePos, so this is the matching read-only accessor; GetPos is the
  // transformed/effective coordinate and cannot be mixed with a 195x195 local
  // radar space.
  Vec3 GetRelativePos(IUIElement* element) const;
  void SetPos(IUIElement* element, const Vec3& p) const;
  Vec3 GetScale(IUIElement* element) const;
  void SetScale(IUIElement* element, const Vec3& s) const;
  float GetSizeX(IUIElement* element) const;
  float GetSizeY(IUIElement* element) const;
  float GetUnscaledSizeX(IUIElement* element) const;
  float GetUnscaledSizeY(IUIElement* element) const;
  bool IsVisible(IUIElement* element) const;
  void SetVisible(IUIElement* element, bool visible) const;
  void SetOpacity(IUIElement* element, float opacity) const;
  int ChildCount(IUIElement* element) const;
  IUIElement* Child(IUIElement* element, int index) const;

  // Native menu access. Paths and actions go through the engine's own UI
  // object model; no keyboard/controller events are synthesized, so touching
  // the panel cannot leak into character controls.
  IUIElement* FindElementByPath(IUIElement* root, const char* path) const;
  IUIElement* FindElement(IUIElement* root, const char* id,
                          bool recursive = true) const;
  bool IsUiControl(IUIElement* element) const;
  bool IsUiText(IUIElement* element) const;
  bool IsUiImage(IUIElement* element) const;
  // Returns the authored/runtime texture identifier of an image element. The
  // identifier is useful even though the custom CPU canvas cannot sample the
  // engine-owned texture directly: it lets the UI choose the correct native
  // item/skill glyph instead of guessing from a translated display name.
  std::string ImageTextureName(IUIElement* element) const;
  std::string ControlText(IUIElement* control, int index = 0) const;
  bool SelectControl(IUIElement* control) const;
  bool ClickControl(IUIElement* control) const;
  bool IsControlSelected(IUIElement* control) const;
  bool IsControlEnabled(IUIElement* control) const;

  // What a text element is displaying.
  //
  // ElementText proves that the object hierarchy contains IUIText via MSVC
  // RTTI, then calls the verified exported base implementation directly.  The
  // abstract exported vtable is not the concrete text-control vtable and must
  // not be used to guess this late virtual slot.
  std::string ElementText(IUIElement* element) const;

 private:
  void* Find(const char* mangled, const char* pretty);

  bool ready_ = false;
  std::vector<Symbol> symbols_;

  // Resolved addresses, in the order they are used above.
  void* level_get_ui_manager_ = nullptr;
  void* level_get_visible_screens_ = nullptr;
  void* level_get_loaded_screens_ = nullptr;
  void* screen_get_script_name_ = nullptr;
  void* screen_get_id_ = nullptr;
  void* screen_show_ = nullptr;
  void* screen_hide_ = nullptr;
  void* screen_set_layer_ = nullptr;
  void* element_get_pos_ = nullptr;
  void* element_get_relative_pos_ = nullptr;
  void* element_set_pos_ = nullptr;
  void* element_get_scale_ = nullptr;
  void* element_set_scale_ = nullptr;
  void* element_get_size_x_ = nullptr;
  void* element_get_size_y_ = nullptr;
  void* element_get_unscaled_size_x_ = nullptr;
  void* element_get_unscaled_size_y_ = nullptr;
  void* element_is_visible_ = nullptr;
  void* element_set_visible_ = nullptr;
  void* element_set_opacity_ = nullptr;
  void* element_children_count_ = nullptr;
  void* element_get_children_ = nullptr;
  void* level_is_registered_ = nullptr;
  void* level_is_loaded_ = nullptr;
  void* level_is_visible_ = nullptr;
  void* level_find_screen_ = nullptr;
  void* level_ui_manager_visible_ = nullptr;
  void* level_is_loading_ = nullptr;
  void* level_screen_id_by_name_ = nullptr;
  void* level_load_screen_ = nullptr;
  void* level_show_screen_ = nullptr;
  void* level_hide_screen_ = nullptr;
  void* level_world_position_ = nullptr;
  void* level_forward_vector_ = nullptr;
  void* level_terrain_size_ = nullptr;
  void* level_name_ = nullptr;
  void* level_get_var_set_ = nullptr;
  void* object_get_float_ = nullptr;
  void* object_get_int_ = nullptr;
  void* text_get_text_ = nullptr;
  void* image_get_texture_name_ = nullptr;
  void* element_find_by_path_ = nullptr;
  void* element_find_ = nullptr;
  void* control_get_text_ = nullptr;
  void* control_select_ = nullptr;
  void* control_click_ = nullptr;
  void* control_is_selected_ = nullptr;
  void* control_is_enabled_ = nullptr;
  int control_text_slot_ = -1;
  int control_click_slot_ = -1;
};

Engine& TheEngine();

}  // namespace dis
