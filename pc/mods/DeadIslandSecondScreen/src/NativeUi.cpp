#include "NativeUi.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "Log.h"
#include "Mem.h"
#include "GameplayBridge.h"

namespace dis {
namespace {

constexpr int kMaxChildrenPerNode = 96;
constexpr int kMaxListRows = 96;
constexpr int kMaxControlTextFields = 4;
constexpr std::size_t kMaxOwnedTextBytes = 4096;

constexpr const char* kInventoryScreen = "MenuInventory";
constexpr const char* kMissionsScreen = "MenuQuestLog";
constexpr const char* kSkillsScreen = "MenuUpgradeCharacter";

// GetUIScreenIdByScriptName only searches the live level/UI-manager tables in
// this build. The game registers these authored menus during gamedll startup
// and stores the returned ids in fixed globals; those are the same ids passed
// to ILevel::LoadUIScreen by the vanilla menu. Reading the verified globals is
// the missing bootstrap path for a menu that has never been opened yet.
constexpr uint32_t kGameTimestamp = 0x5745B64C;
constexpr uint32_t kGameImageSize = 0x014C8000;
constexpr uintptr_t kInventoryScreenIdRva = 0x0123DA28;
constexpr uintptr_t kMissionsScreenIdRva = 0x0123D9EC;
constexpr uintptr_t kSkillsScreenIdRva = 0x0123D9F0;
constexpr std::size_t kOpenGameplayMenuVtableOffset = 0x5B0;

std::atomic<ILevel*> g_native_menu_host{nullptr};

ILevel* NativeMenuHost(ILevel* fallback) {
  ILevel* host = g_native_menu_host.load(std::memory_order_acquire);
  return host ? host : fallback;
}

bool SupportedRegistrationBuild(uint8_t** image_out) {
  if (image_out) *image_out = nullptr;
  HMODULE game = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
  if (!game) return false;
  auto* image = reinterpret_cast<uint8_t*>(game);
  IMAGE_DOS_HEADER dos = {};
  IMAGE_NT_HEADERS64 nt = {};
  if (!Read(image, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew <= 0 || !Read(image + dos.e_lfanew, &nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.FileHeader.TimeDateStamp != kGameTimestamp ||
      nt.OptionalHeader.SizeOfImage != kGameImageSize) {
    return false;
  }
  if (image_out) *image_out = image;
  return true;
}

int AuthoredScreenId(const char* script_name) {
  if (!script_name || !*script_name) return -1;
  uintptr_t rva = 0;
  if (std::strcmp(script_name, kInventoryScreen) == 0) {
    rva = kInventoryScreenIdRva;
  } else if (std::strcmp(script_name, kMissionsScreen) == 0) {
    rva = kMissionsScreenIdRva;
  } else if (std::strcmp(script_name, kSkillsScreen) == 0) {
    rva = kSkillsScreenIdRva;
  } else {
    return -1;
  }

  uint8_t* image = nullptr;
  int id = -1;
  if (!SupportedRegistrationBuild(&image) ||
      !Read(image + rva, &id) || id < 0) {
    return -1;
  }

  static int logged_inventory = -1;
  static int logged_missions = -1;
  static int logged_skills = -1;
  int* logged = nullptr;
  if (rva == kInventoryScreenIdRva) logged = &logged_inventory;
  if (rva == kMissionsScreenIdRva) logged = &logged_missions;
  if (rva == kSkillsScreenIdRva) logged = &logged_skills;
  if (logged && *logged != id) {
    DIS_INFO("[native] %s authored registration id = %d", script_name, id);
    *logged = id;
  }
  return id;
}

bool RequestAuthoredMenu(ILevel* level, int id) {
  level = NativeMenuHost(level);
  if (!level || id < 0 || !SupportedRegistrationBuild(nullptr)) return false;

  // Dead Island's own Inventory/Skills entry points do not call the exported
  // ILevel::LoadUIScreen function. They dispatch this derived-level virtual:
  //
  //   level->vfunc_5B0(screen_id, data=null, show=true, selection=-1)
  //
  // That wrapper constructs the gamedll controller and then shows its XUI.
  // Calling LoadUIScreen directly only asks the engine for an already-live
  // registration and returns null for these gameplay-authored menus. The
  // vanilla entry point first calls another virtual that changes the normal
  // gameplay/menu mode; deliberately omitting that separate call keeps the
  // top-screen game unpaused while the transparent companion controller is
  // alive for the bottom screen.
  void* raw_vtable = nullptr;
  if (!Read(level, &raw_vtable) || !raw_vtable) return false;
  auto** vtable = static_cast<void**>(raw_vtable);
  void* target = nullptr;
  const std::size_t slot =
      kOpenGameplayMenuVtableOffset / sizeof(void*);
  if (!Read(vtable + slot, &target) || !target || !Executable(target, 1)) {
    return false;
  }

  using FnOpenGameplayMenu =
      void (*)(ILevel*, int, void*, bool, int);
  reinterpret_cast<FnOpenGameplayMenu>(target)(level, id, nullptr, true, -1);
  return true;
}

struct ScreenRef {
  IUIElement* root = nullptr;
  int id = -1;
  std::string reason;
};

std::string OwnedText(std::string value) {
  // Do not let a malformed/modded XUI value turn a snapshot into an unbounded
  // allocation.  Native strings are normally tiny; 4 KiB still leaves ample
  // room for quest and skill descriptions.
  const std::size_t nul = value.find('\0');
  if (nul != std::string::npos) value.resize(nul);
  if (value.size() > kMaxOwnedTextBytes) value.resize(kMaxOwnedTextBytes);

  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' ||
          value.back() == '\r' || value.back() == '\n')) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() &&
         (value[first] == ' ' || value[first] == '\t' ||
          value[first] == '\r' || value[first] == '\n')) {
    ++first;
  }
  if (first != 0) value.erase(0, first);
  return value;
}

ScreenRef NativeScreen(ILevel* level, const char* script_name) {
  ScreenRef result;
  Engine& engine = TheEngine();
  level = NativeMenuHost(level);
  if (!level) {
    result.reason = "No active level.";
    return result;
  }
  if (!engine.ready()) {
    result.reason = "Native UI exports are unavailable.";
    return result;
  }
  if (!engine.IsUIManagerVisible(level)) {
    result.reason = "The native UI manager is not ready.";
    return result;
  }

  result.id = engine.ScreenIdByScriptName(level, script_name);
  if (result.id < 0) {
    // Some builds retain a bare filename alias. Keep it ahead of the authored
    // path candidates for compatibility with those registration tables.
    const std::string filename = std::string(script_name) + ".xui";
    result.id = engine.ScreenIdByScriptName(level, filename.c_str());
  }
  if (result.id < 0) {
    // This is the exact case-sensitive form registered by the shipped game:
    // its helper prepends the lowercase backslash path to the CamelCase XUI
    // filename. GetUIScreenIdByScriptName performs an exact comparison.
    const std::string runtime =
        std::string("data\\menu\\scr\\") + script_name + ".xui";
    result.id = engine.ScreenIdByScriptName(level, runtime.c_str());
  }
  if (result.id < 0) {
    const std::string runtime =
        std::string("data/menu/scr/") + script_name + ".xui";
    result.id = engine.ScreenIdByScriptName(level, runtime.c_str());
  }
  if (result.id < 0) {
    std::string filename = std::string(script_name) + ".xui";
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    result.id = engine.ScreenIdByScriptName(level, filename.c_str());
  }
  if (result.id < 0) {
    const std::string authored =
        std::string("Data/Menu/Scr/") + script_name + ".xui";
    result.id = engine.ScreenIdByScriptName(level, authored.c_str());
  }
  if (result.id < 0) {
    std::string authored =
        std::string("data/menu/scr/") + script_name + ".xui";
    std::transform(authored.begin(), authored.end(), authored.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    result.id = engine.ScreenIdByScriptName(level, authored.c_str());
    if (result.id < 0) {
      std::replace(authored.begin(), authored.end(), '/', '\\');
      result.id = engine.ScreenIdByScriptName(level, authored.c_str());
    }
  }
  if (result.id < 0) result.id = AuthoredScreenId(script_name);
  if (result.id < 0) {
    result.reason = std::string(script_name) + " has no registered script id.";
    return result;
  }

  // GetUIScreenIdByScriptName searches both the loaded-screen vector and the
  // UI manager's authored script table. IsUIScreenRegistered, however, only
  // checks the live registration vector. A valid authored id therefore often
  // reports false until LoadUIScreen has constructed its shell. Requiring both
  // predicates here made Inventory and Skills impossible to bootstrap: the
  // bridge returned before it ever reached LoadUIScreen. Loaded/visible/root
  // checks below remain the authoritative safety gate for traversal.

  // Do not construct a native menu just because its custom page was selected.
  // Load creates only an authored shell; controller backing appears on Show
  // and is destroyed again on Hide. Loading hidden shells therefore cannot
  // make these adapters useful and only adds state during gameplay. Native
  // fallback access is limited to a screen the game already loaded and showed.
  if (!engine.IsScreenLoaded(level, result.id)) {
    result.reason = std::string(script_name) + " has not been opened by the game.";
    return result;
  }
  IUIScreen* screen =
      reinterpret_cast<IUIScreen*>(engine.FindScreen(level, result.id));
  if (!screen) {
    result.reason = std::string(script_name) + " could not be acquired.";
    return result;
  }

  const std::string actual = engine.ScreenName(screen);
  if (!actual.empty()) {
    std::string actual_lower = actual;
    std::string wanted_lower = script_name;
    const auto lower = [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    };
    std::transform(actual_lower.begin(), actual_lower.end(),
                   actual_lower.begin(), lower);
    std::transform(wanted_lower.begin(), wanted_lower.end(),
                   wanted_lower.begin(), lower);
    if (actual_lower.find(wanted_lower) == std::string::npos) {
      result.reason = "Native screen identity changed; access was refused.";
      return result;
    }
  }

  // The dynamic controls owned by these menus are not valid merely because
  // the authored XUI screen remains loaded. MenuInventory::OnHide releases
  // backing item objects, and MenuQuestLog::OnHide clears its controller-side
  // row vector. Child IsVisible flags can remain true afterward, so treating
  // a hidden tree as live risks copying stale labels or invoking callbacks on
  // released data. Only traverse/click while the UI manager itself reports
  // this screen visible. A future direct gameplay-data bridge can replace this
  // fail-closed gate without reviving hidden native controls.
  if (!engine.IsScreenVisible(level, result.id)) {
    result.reason = std::string(script_name) +
        " is hidden; its native controller backing is not safe to access.";
    return result;
  }
  result.root = reinterpret_cast<IUIElement*>(screen);
  return result;
}

int RegisteredScreenId(ILevel* level, const char* script_name) {
  Engine& engine = TheEngine();
  level = NativeMenuHost(level);
  if (!level || !script_name || !*script_name || !engine.ready() ||
      !engine.IsUIManagerVisible(level)) return -1;
  int id = engine.ScreenIdByScriptName(level, script_name);
  if (id < 0) {
    const std::string filename = std::string(script_name) + ".xui";
    id = engine.ScreenIdByScriptName(level, filename.c_str());
  }
  if (id < 0) {
    const std::string runtime =
        std::string("data\\menu\\scr\\") + script_name + ".xui";
    id = engine.ScreenIdByScriptName(level, runtime.c_str());
  }
  if (id < 0) {
    const std::string runtime =
        std::string("data/menu/scr/") + script_name + ".xui";
    id = engine.ScreenIdByScriptName(level, runtime.c_str());
  }
  if (id < 0) {
    std::string filename = std::string(script_name) + ".xui";
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    id = engine.ScreenIdByScriptName(level, filename.c_str());
  }
  if (id < 0) {
    const std::string authored =
        std::string("Data/Menu/Scr/") + script_name + ".xui";
    id = engine.ScreenIdByScriptName(level, authored.c_str());
  }
  if (id < 0) {
    std::string authored =
        std::string("data/menu/scr/") + script_name + ".xui";
    std::transform(authored.begin(), authored.end(), authored.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    id = engine.ScreenIdByScriptName(level, authored.c_str());
    if (id < 0) {
      std::replace(authored.begin(), authored.end(), '/', '\\');
      id = engine.ScreenIdByScriptName(level, authored.c_str());
    }
  }
  if (id < 0) id = AuthoredScreenId(script_name);
  static bool inventory_missing_logged = false;
  static bool skills_missing_logged = false;
  static bool inventory_deferred_logged = false;
  static bool skills_deferred_logged = false;
  bool* missing_logged = nullptr;
  bool* deferred_logged = nullptr;
  if (std::strcmp(script_name, kInventoryScreen) == 0) {
    missing_logged = &inventory_missing_logged;
    deferred_logged = &inventory_deferred_logged;
  } else if (std::strcmp(script_name, kSkillsScreen) == 0) {
    missing_logged = &skills_missing_logged;
    deferred_logged = &skills_deferred_logged;
  }

  if (id < 0) {
    if (!missing_logged || !*missing_logged) {
      DIS_WARN("[native] no authored screen id found for %s", script_name);
      if (missing_logged) *missing_logged = true;
    }
    return -1;
  }

  // See NativeScreen above. A script-table id is sufficient to call the
  // engine's bounded LoadUIScreen wrapper; requiring the separate live-vector
  // predicate here was the circular gate that left every page empty.
  if (!engine.IsScreenRegistered(level, id) &&
      (!deferred_logged || !*deferred_logged)) {
    DIS_INFO("[native] %s resolved to screen %d before live registration; "
             "loading its authored controller", script_name, id);
    if (deferred_logged) *deferred_logged = true;
  }
  return id;
}

IUIElement* FindKnown(IUIElement* root,
                      std::initializer_list<const char*> paths) {
  if (!root) return nullptr;
  Engine& engine = TheEngine();
  for (const char* path : paths) {
    if (!path || !*path) continue;
    if (IUIElement* found = engine.FindElementByPath(root, path)) return found;
  }
  // The exported recursive ID lookup is the safe fallback for paths authored
  // relative to a different root. It avoids GetDebugId's by-value string ABI
  // and never walks or guesses arbitrary object types.
  for (const char* path : paths) {
    if (!path || !*path) continue;
    const char* id = path;
    for (const char* p = path; *p; ++p) {
      if (*p == '/' || *p == '\\' || *p == '.') id = p + 1;
    }
    if (IUIElement* found = engine.FindElement(root, id, true)) return found;
  }
  return nullptr;
}

IUIElement* FindExact(IUIElement* root,
                      std::initializer_list<const char*> paths) {
  if (!root) return nullptr;
  Engine& engine = TheEngine();
  for (const char* path : paths) {
    if (!path || !*path) continue;
    if (IUIElement* found = engine.FindElementByPath(root, path)) return found;
  }
  return nullptr;
}

std::vector<std::string> ControlStrings(IUIElement* control) {
  std::vector<std::string> result;
  if (!control) return result;
  Engine& engine = TheEngine();
  result.reserve(kMaxControlTextFields);
  for (int i = 0; i < kMaxControlTextFields; ++i) {
    std::string value = OwnedText(engine.ControlText(control, i));
    // Data-association slots are positional. Do not deduplicate equal rank or
    // amount strings: sibling skill controls commonly expose the same "0/3"
    // text, and equality is not evidence that they are one control. Empty
    // interior fields are retained as well, otherwise association 2 silently
    // becomes field 0 or 1 and can be mistaken for a display label/rank.
    result.push_back(std::move(value));
  }
  while (!result.empty() && result.back().empty()) result.pop_back();
  return result;
}

bool LooksLikeTextureKey(const std::string& value) {
  if (value.size() < 3 || value.size() > 260) return false;
  bool has_alpha = false;
  bool has_texture_shape = false;
  for (unsigned char c : value) {
    if (std::isspace(c) || std::iscntrl(c)) return false;
    if (std::isalpha(c)) has_alpha = true;
    if (c == '_' || c == '/' || c == '\\' || c == '.') {
      has_texture_shape = true;
    }
  }
  std::string lower = value;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (lower.find("skill") != std::string::npos ||
      lower.find("icon") != std::string::npos ||
      lower.find("texture") != std::string::npos ||
      lower.find(".dds") != std::string::npos) {
    has_texture_shape = true;
  }
  return has_alpha && has_texture_shape;
}

std::string ReadText(IUIElement* root,
                     std::initializer_list<const char*> paths) {
  if (!root) return {};
  Engine& engine = TheEngine();
  // A present-but-empty standard/SD sibling must not mask a later populated
  // path. This is common in the HUD and skill menu, where both layouts remain
  // authored under the same root.
  for (const char* path : paths) {
    if (!path || !*path) continue;
    IUIElement* element = engine.FindElementByPath(root, path);
    if (!element) continue;
    std::string value = OwnedText(engine.ElementText(element));
    if (!value.empty()) return value;
  }
  for (const char* path : paths) {
    if (!path || !*path) continue;
    const char* id = path;
    for (const char* p = path; *p; ++p)
      if (*p == '/' || *p == '\\' || *p == '.') id = p + 1;
    IUIElement* element = engine.FindElement(root, id, true);
    if (!element) continue;
    std::string value = OwnedText(engine.ElementText(element));
    if (!value.empty()) return value;
  }
  return {};
}

std::string ReadExactText(IUIElement* root,
                          std::initializer_list<const char*> paths) {
  if (!root) return {};
  Engine& engine = TheEngine();
  for (const char* path : paths) {
    if (!path || !*path) continue;
    IUIElement* element = engine.FindElementByPath(root, path);
    if (!element) continue;
    std::string value = OwnedText(engine.ElementText(element));
    if (!value.empty()) return value;
  }
  return {};
}

std::string ReadImageTexture(IUIElement* root,
                             std::initializer_list<const char*> paths) {
  if (!root) return {};
  Engine& engine = TheEngine();
  for (const char* path : paths) {
    if (!path || !*path) continue;
    IUIElement* image = engine.FindElementByPath(root, path);
    if (!image) continue;
    std::string value = OwnedText(engine.ImageTextureName(image));
    if (!value.empty()) return value;
  }
  for (const char* path : paths) {
    if (!path || !*path) continue;
    const char* id = path;
    for (const char* p = path; *p; ++p)
      if (*p == '/' || *p == '\\' || *p == '.') id = p + 1;
    IUIElement* image = engine.FindElement(root, id, true);
    if (!image) continue;
    std::string value = OwnedText(engine.ImageTextureName(image));
    if (!value.empty()) return value;
  }
  return {};
}

bool CopyRadarVisual(IUIElement* element, const char* fallback_texture,
                     float fallback_width, float fallback_height,
                     HudRadarVisual* out) {
  if (!element || !out) return false;
  Engine& engine = TheEngine();
  const Vec3 position = engine.GetRelativePos(element);
  const Vec3 scale = engine.GetScale(element);
  float width = engine.GetUnscaledSizeX(element);
  float height = engine.GetUnscaledSizeY(element);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      std::fabs(position.x) > 1024.0f || std::fabs(position.y) > 1024.0f) {
    return false;
  }
  if (!std::isfinite(width) || width <= 0.0f || width > 512.0f)
    width = fallback_width;
  if (!std::isfinite(height) || height <= 0.0f || height > 512.0f)
    height = fallback_height;
  float scale_x = std::isfinite(scale.x) ? std::fabs(scale.x) : 1.0f;
  float scale_y = std::isfinite(scale.y) ? std::fabs(scale.y) : 1.0f;
  if (scale_x <= 0.001f || scale_x > 8.0f) scale_x = 1.0f;
  if (scale_y <= 0.001f || scale_y > 8.0f) scale_y = 1.0f;

  out->x = position.x;
  out->y = position.y;
  out->width = width;
  out->height = height;
  out->scale_x = scale_x;
  out->scale_y = scale_y;
  out->texture = OwnedText(engine.ImageTextureName(element));
  if (out->texture.empty() && fallback_texture)
    out->texture = fallback_texture;
  return true;
}

HudRadarSnapshot BuildHudRadar(IUIElement* hud_root) {
  HudRadarSnapshot out;
  if (!hud_root) return out;
  Engine& engine = TheEngine();
  IUIElement* radar = FindKnown(hud_root, {"HudRadar"});
  IUIElement* group = FindKnown(radar, {"G_Group"});
  IUIElement* path_group = FindKnown(group, {"G_Path"});
  IUIElement* player = FindKnown(group, {"I_Player"});
  IUIElement* dummy_marker = FindKnown(group, {"G_DUMMY"});
  IUIElement* radar_layer = FindKnown(group, {"G_Radar"});
  if (!radar || !group || !path_group || !player ||
      !CopyRadarVisual(player, "radar_icon_player", 32.0f, 32.0f,
                       &out.player)) {
    return out;
  }

  constexpr int kMaxRadarVisuals = 128;
  const Vec3 path_origin = engine.GetRelativePos(path_group);
  const int path_children = std::clamp(engine.ChildCount(path_group), 0,
                                       kMaxRadarVisuals);
  out.path.reserve(path_children);
  for (int i = 0; i < path_children; ++i) {
    IUIElement* child = engine.Child(path_group, i);
    if (!child || !engine.IsVisible(child) || !engine.IsUiImage(child))
      continue;
    HudRadarVisual visual;
    if (!CopyRadarVisual(child, nullptr, 5.0f, 6.0f, &visual) ||
        (visual.texture.find("radar_path") == std::string::npos &&
         visual.texture.find("dn_arrow") == std::string::npos)) {
      continue;
    }
    if (std::isfinite(path_origin.x) && std::isfinite(path_origin.y)) {
      visual.x += path_origin.x;
      visual.y += path_origin.y;
    }
    out.path.push_back(std::move(visual));
  }

  const int marker_children = std::clamp(engine.ChildCount(group), 0,
                                         kMaxRadarVisuals);
  out.markers.reserve(marker_children);
  for (int i = 0; i < marker_children; ++i) {
    IUIElement* child = engine.Child(group, i);
    if (!child || child == path_group || child == player ||
        child == dummy_marker || child == radar_layer ||
        !engine.IsVisible(child)) {
      continue;
    }
    IUIElement* icon = FindKnown(child, {"I_Icon"});
    if (!icon || !engine.IsVisible(icon) || !engine.IsUiImage(icon)) continue;

    // Position belongs to the cloned HudRadarElement; artwork and text are
    // nested children of that clone. Copy the final controller-computed local
    // placement, never the private navigation/path model behind it.
    HudRadarVisual visual;
    if (!CopyRadarVisual(child, nullptr, 32.0f, 32.0f, &visual)) continue;
    visual.texture = OwnedText(engine.ImageTextureName(icon));
    if (visual.texture.empty() ||
        visual.texture.find("radar_") == std::string::npos) {
      continue;
    }
    visual.label = ReadExactText(child, {"T_Text"});
    if (IUIElement* up = FindKnown(child, {"I_Up"}))
      visual.up = engine.IsVisible(up);
    out.markers.push_back(std::move(visual));
  }
  out.available = true;
  return out;
}

HudSnapshot BuildHud(ILevel* level) {
  HudSnapshot out;
  Engine& engine = TheEngine();
  constexpr int kHudScreen = 1024;
  if (!level || !engine.ready() || !engine.IsUIManagerVisible(level) ||
      !engine.IsScreenLoaded(level, kHudScreen)) {
    return out;
  }
  IUIElement* root = engine.FindScreen(level, kHudScreen);
  if (!root || !engine.IsScreenVisible(level, kHudScreen)) return out;
  out.available = true;
  out.radar = BuildHudRadar(root);
  // Resolve each unique top-level controller first. FindElementByPath is
  // authored relative to an element rather than to IUIScreen on this engine
  // build; feeding it a full HUD_DI/... path made every exact field appear
  // empty even though the controller itself was live.
  IUIElement* held = FindKnown(root, {"HudHeldItem"});
  IUIElement* circle = FindKnown(root, {"HudInventoryCircle"});
  IUIElement* inventory = FindKnown(root, {"HudInventory"});
  IUIElement* player = FindKnown(root, {"HudPlayer"});
  // HudPlayer owns the live progression presenter used by the vanilla HUD.
  // Reading its rendered fraction avoids the zero-valued LogicalPlayer
  // replication mirror and stays correct when XP changes between samples.
  out.xp = ReadExactText(
      player, {"G_Group/G_XpText/T_Xp", "G_XpText/T_Xp"});
  if (out.xp.empty()) {
    out.xp = ReadExactText(
        player, {"G_Group/G_XpText/T_XpBack", "G_XpText/T_XpBack"});
  }
  out.equipped_name = ReadExactText(
      held, {"G_Inner/T_Text"});
  if (out.equipped_name.empty()) {
    out.equipped_name = ReadExactText(
        circle, {"G_Group/G_ItemInfo/G_Basic/T_Name"});
  }
  out.icon_texture = ReadImageTexture(
      circle, {"G_Group/G_Item/G_CounterRotate/I_Item"});
  if (out.icon_texture.empty())
    out.icon_texture = ReadImageTexture(inventory, {"I_Item"});
  out.clip_ammo = ReadExactText(
      circle, {"G_Group/G_Item/G_CounterRotate/G_Ammo/T_Ammo"});
  out.total_ammo = ReadExactText(
      circle, {"G_Group/G_Item/G_CounterRotate/G_Ammo/T_Ammo2"});
  out.held_quantity = ReadExactText(
      circle, {"G_Group/G_Item/G_CounterRotate/G_Qty/T_Qty"});
  out.durability = ReadExactText(
      held, {"G_Inner/G_ItemInfo/G_Value/T_Value"});
  if (out.durability.empty()) {
    out.durability = ReadExactText(
        circle, {"G_Group/G_ItemInfo/G_Value/T_Value"});
  }
  for (int i = 0; i < 4; ++i) {
    const std::string held_path =
        "G_Inner/G_ItemInfo/G_Stats/T_Value" + std::to_string(i);
    const std::string circle_path =
        "G_Group/G_ItemInfo/G_Stats/T_Value" + std::to_string(i);
    std::string value = ReadExactText(held, {held_path.c_str()});
    if (value.empty())
      value = ReadExactText(circle, {circle_path.c_str()});
    out.item_stats.push_back(std::move(value));
  }
  return out;
}

IUIElement* ListItemGroup(IUIElement* list) {
  return FindKnown(list, {"ItemGroup"});
}

std::vector<IUIElement*> VisibleListControls(IUIElement* list) {
  std::vector<IUIElement*> result;
  if (!list) return result;
  Engine& engine = TheEngine();
  IUIElement* group = ListItemGroup(list);
  if (!group) return result;

  const int children = std::max(0, std::min(engine.ChildCount(group),
                                             kMaxListRows));
  for (int i = 0; i < children; ++i) {
    IUIElement* child = engine.Child(group, i);
    if (!child || !engine.IsVisible(child) || !engine.IsUiControl(child)) {
      continue;
    }
    // ItemGroup's direct visible children are the list visual's runtime
    // UIButton/UIButtonDoubleSelect rows (including category controls). The
    // hidden templates are filtered by IsVisible above.
    result.push_back(child);
  }
  return result;
}

NativeUiRow CopyControlRow(IUIElement* control, int group = -1, int tier = -1,
                           int native_ordinal = -1) {
  NativeUiRow row;
  if (!control) return row;
  Engine& engine = TheEngine();
  // Runtime inventory and skill buttons use one of these two authored ids.
  // Prefer I_Item because inventory controls often also contain an I_Icon
  // decoration whose texture is only the slot frame.
  row.icon_texture = ReadImageTexture(control,
      {"I_Item", "G_Item/I_Unequip", "I_Unequip", "I_ItemIcon",
       "I_SkillIcon", "I_Icon"});
  row.text = ControlStrings(control);
  if (row.icon_texture.empty()) {
    // Inventory list presenters commonly expose their exact atlas rectangle
    // (for example inv_wpn_paddle_a) as a data association rather than as an
    // IUIImage child. Preserve it whenever the controller makes it available.
    for (const std::string& value : row.text) {
      if (!LooksLikeTextureKey(value)) continue;
      std::string lower = value;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                     });
      if (lower.find("inv_wpn_") != std::string::npos ||
          lower.find("inv_slot_") != std::string::npos ||
          lower.find("icon_item_") != std::string::npos ||
          lower.find("weapon_type_") != std::string::npos) {
        row.icon_texture = value;
        break;
      }
    }
  }
  if (row.icon_texture.empty()) {
    // The shipped ListButtonInventoryV, ButtonItemV and SkillButtonV visuals
    // bind their live art through UIImagePresenter. The only exported route
    // from a presenter to its parent texture is
    // IUIElementControl::GetTexture(int), which returns ttl::string_base by
    // value. Calling it without the engine string's exact construction and
    // destruction ABI is not crash-safe. Preserve the exact presenter source
    // instead; the custom renderer can use this stable semantic identifier,
    // and a future verified texture bridge can resolve it without guessing.
    static const char* const kPresenterPaths[] = {
        "G_Item/I_Equip", "UIImagePresenter1", "icon",
        "BackgroundColor_ImgPrezenter", "Circle_ImgPrezenter"};
    for (const char* path : kPresenterPaths) {
      if (engine.FindElementByPath(control, path)) {
        row.icon_texture = std::string("xui-presenter:") + path;
        break;
      }
    }
  }
  // SkillButtonV's live icon presenter is DataAssociation 2. On builds where
  // the concrete button exposes that association through GetText, retain the
  // exact atlas key. Do not treat arbitrary prose/numeric values as texture
  // ids; the verified presenter path above remains the fail-closed fallback.
  if (group >= 0 && tier >= 0 && row.text.size() > 2 &&
      LooksLikeTextureKey(row.text[2])) {
    row.icon_texture = row.text[2];
  }
  row.native_ordinal = native_ordinal;
  row.id = "native:" + std::to_string(group) + ":" +
           std::to_string(tier) + ":" + std::to_string(native_ordinal);
  row.selected = engine.IsControlSelected(control);
  row.enabled = engine.IsControlEnabled(control);
  row.group = group;
  row.tier = tier;
  const Vec3 position = engine.GetPos(control);
  if (std::isfinite(position.x) && std::isfinite(position.y) &&
      std::fabs(position.x) < 100000.0f && std::fabs(position.y) < 100000.0f) {
    row.native_x = position.x;
    row.native_y = position.y;
    row.has_native_position = true;
  }
  return row;
}

std::vector<NativeUiRow> CopyList(IUIElement* list) {
  std::vector<NativeUiRow> result;
  const std::vector<IUIElement*> controls = VisibleListControls(list);
  result.reserve(controls.size());
  for (std::size_t i = 0; i < controls.size(); ++i) {
    result.push_back(CopyControlRow(controls[i], -1, -1,
                                    static_cast<int>(i)));
  }
  return result;
}

std::string SelectedRowText(const std::vector<NativeUiRow>& rows) {
  for (const NativeUiRow& row : rows) {
    if (row.selected && !row.text.empty()) return row.text.front();
  }
  return {};
}

std::string RowsText(const std::vector<NativeUiRow>& rows) {
  std::string result;
  for (const NativeUiRow& row : rows) {
    for (const std::string& value : row.text) {
      if (value.empty()) continue;
      if (!result.empty()) result += '\n';
      const std::size_t room = kMaxOwnedTextBytes - result.size();
      if (room == 0) return result;
      result.append(value.data(), std::min(room, value.size()));
    }
  }
  return result;
}

std::vector<IUIElement*> SkillControls(IUIElement* root) {
  std::vector<IUIElement*> result;
  if (!root) return result;
  Engine& engine = TheEngine();
  for (int tree = 0; tree < 3; ++tree) {
    const std::string tree_path = "G_Buttons/G_Tree" + std::to_string(tree);
    IUIElement* tree_root = FindKnown(root, {tree_path.c_str()});
    if (!tree_root) continue;
    for (int tier = 0; tier < 7; ++tier) {
      const std::string tier_id = "G_Tier" + std::to_string(tier);
      IUIElement* tier_root = FindKnown(tree_root, {tier_id.c_str()});
      if (!tier_root) continue;
      const int children = std::max(0, std::min(engine.ChildCount(tier_root),
                                                 kMaxChildrenPerNode));
      for (int i = 0; i < children &&
                      static_cast<int>(result.size()) < kMaxListRows; ++i) {
        IUIElement* child = engine.Child(tier_root, i);
        // Locked and not-currently-focusable skills can have Show=false while
        // still being real controller-created children. Tier groups contain
        // no authored button templates, so retaining every typed direct child
        // exposes the complete tree without recursively collecting unrelated
        // decorations. ControlUsable still rejects hidden nodes on actions.
        if (!child || !engine.IsUiControl(child)) continue;
        result.push_back(child);
      }
    }
  }
  return result;
}

std::vector<NativeUiRow> CopySkills(IUIElement* root) {
  std::vector<NativeUiRow> rows;
  if (!root) return rows;
  Engine& engine = TheEngine();
  for (int tree = 0; tree < 3; ++tree) {
    const std::string tree_path = "G_Buttons/G_Tree" + std::to_string(tree);
    IUIElement* tree_root = FindKnown(root, {tree_path.c_str()});
    if (!tree_root) continue;
    for (int tier = 0; tier < 7; ++tier) {
      const std::string tier_id = "G_Tier" + std::to_string(tier);
      IUIElement* tier_root = FindKnown(tree_root, {tier_id.c_str()});
      if (!tier_root) continue;
      const int children = std::max(0, std::min(engine.ChildCount(tier_root),
                                                 kMaxChildrenPerNode));
      for (int i = 0; i < children &&
                      static_cast<int>(rows.size()) < kMaxListRows; ++i) {
        IUIElement* child = engine.Child(tier_root, i);
        if (!child || !engine.IsUiControl(child)) {
          continue;
        }
        NativeUiRow row = CopyControlRow(child, tree, tier, i);
        // GetPos(child) is local to its tier. Include the tier's authored Y
        // offset so the snapshot retains the actual branching topology instead
        // of placing all seven tiers at y=0. Tree itself remains represented by
        // row.group, so its column offset is intentionally not folded into X.
        const Vec3 tier_position = engine.GetPos(tier_root);
        if (row.has_native_position && std::isfinite(tier_position.x) &&
            std::isfinite(tier_position.y) &&
            std::fabs(tier_position.x) < 100000.0f &&
            std::fabs(tier_position.y) < 100000.0f) {
          row.native_x += tier_position.x;
          row.native_y += tier_position.y;
        }
        // Hidden controller-created nodes represent locked/unavailable skills,
        // not actionable upgrades. Preserve them for rendering but fail closed
        // in the state exposed to the page and in ControlUsable at activation.
        row.enabled = engine.IsVisible(child) && row.enabled;
        rows.push_back(std::move(row));
      }
    }
  }
  return rows;
}

bool Enabled(IUIElement* root, std::initializer_list<const char*> paths) {
  IUIElement* control = FindExact(root, paths);
  return control && TheEngine().IsVisible(control) &&
         TheEngine().IsControlEnabled(control);
}

InventorySnapshot BuildInventory(const ScreenRef& screen) {
  InventorySnapshot out;
  if (!screen.root) {
    out.unavailable_reason = screen.reason;
    return out;
  }
  IUIElement* list = FindKnown(screen.root,
      {"G_Content/G_Group/L_List", "G_Content.G_Group.L_List", "L_List"});
  if (!list) {
    out.unavailable_reason = "MenuInventory/L_List was not found.";
    return out;
  }
  out.items = CopyList(list);
  if (out.items.empty()) {
    out.unavailable_reason =
        "The native inventory controller has not populated its item rows.";
    return out;
  }
  out.available = true;
  out.selected_name = SelectedRowText(out.items);
  out.description = ReadText(screen.root,
      {"G_Content/G_Group/G_ItemBigFrame/T_DescriptionDI",
       "G_Content/G_Group/G_ItemBigFrame/T_Description"});
  out.price = ReadText(screen.root,
      {"G_Content/G_Group/G_Comparsion2/T_Price", "G_Comparsion2/T_Price",
       "T_Price"});
  out.filter = ReadText(screen.root,
      {"G_Content/G_MenuButtons/T_Filter"});
  out.equip_control_enabled = Enabled(screen.root,
      {"G_Content/G_MenuButtons/B_Equip"});
  out.drop_control_enabled = Enabled(screen.root,
      {"G_Content/G_MenuButtons/B_Drop"});
  out.compare_control_enabled = Enabled(screen.root,
      {"G_Content/G_MenuButtons/B_Compare"});
  out.inspect_control_enabled = Enabled(screen.root,
      {"G_Content/G_MenuButtons/B_Inspect"});
  return out;
}

GearSnapshot BuildGear(const ScreenRef& screen) {
  GearSnapshot out;
  if (!screen.root) {
    out.unavailable_reason = screen.reason;
    return out;
  }
  IUIElement* group = FindKnown(screen.root,
      {"G_Content/G_Group/G_QuickSlots", "G_Content.G_Group.G_QuickSlots",
       "G_QuickSlots"});
  if (!group) {
    out.unavailable_reason = "MenuInventory/G_QuickSlots was not found.";
    return out;
  }
  IUIElement* inventory_list = FindKnown(screen.root,
      {"G_Content/G_Group/L_List"});
  if (!inventory_list || VisibleListControls(inventory_list).empty()) {
    out.unavailable_reason =
        "The native inventory controller has not initialized quick slots.";
    return out;
  }
  out.available = true;
  // Vanilla authors eight slots. Probe one extra ordinal so the custom
  // nine-position footer can interoperate with controller/inventory mods
  // without ever aliasing slot nine onto an existing native action.
  for (int slot = 0; slot < 9; ++slot) {
    const std::string id = "B_Slot" + std::to_string(slot);
    IUIElement* control = FindKnown(group, {id.c_str()});
    NativeUiRow row = CopyControlRow(control, slot, -1, slot);
    row.id = control ? id : std::string();
    row.enabled = control && TheEngine().IsVisible(control) && row.enabled;
    out.quick_slots.push_back(std::move(row));
  }
  return out;
}

MissionsSnapshot BuildMissions(const ScreenRef& screen) {
  MissionsSnapshot out;
  if (!screen.root) {
    out.unavailable_reason = screen.reason;
    return out;
  }
  IUIElement* quests = FindKnown(screen.root,
      {"G_Content/G_Left/G_AlignGroup/L_Quests", "L_Quests"});
  if (!quests) {
    out.unavailable_reason = "MenuQuestLog/L_Quests was not found.";
    return out;
  }
  out.quests = CopyList(quests);
  if (out.quests.empty()) {
    out.unavailable_reason =
        "The native quest controller has not populated its quest rows.";
    return out;
  }
  out.available = true;
  if (IUIElement* phases = FindKnown(screen.root,
          {"G_Content/G_Right/AlignGroup/L_Phases", "L_Phases"})) {
    out.objectives = CopyList(phases);
  }
  out.selected_name = ReadText(screen.root,
      {"G_Content/G_Right/G_Name/T_Name", "G_Name/T_Name", "T_Name"});
  out.reward = ReadText(screen.root,
      {"G_Content/G_Right/G_Name/T_Reward", "G_Name/T_Reward", "T_Reward"});
  // H_PhaseInfo is an HTML presenter, not IUIText. Its exported text accessor
  // is not known, so use the owned objective-row text instead of dispatching a
  // text virtual on the wrong object or exposing the hidden detail template.
  out.description = RowsText(out.objectives);
  out.track_control_enabled = Enabled(screen.root,
      {"G_Content/G_Menu/B_TrackQuest"});
  out.details_control_enabled = Enabled(screen.root,
      {"G_Content/G_Menu/B_ButtonDetails"});
  return out;
}

SkillsSnapshot BuildSkills(const ScreenRef& screen) {
  SkillsSnapshot out;
  if (!screen.root) {
    out.unavailable_reason = screen.reason;
    return out;
  }
  IUIElement* trees = FindKnown(screen.root, {"G_Buttons"});
  if (!trees) {
    out.unavailable_reason = "MenuUpgradeCharacter/G_Buttons was not found.";
    return out;
  }
  out.skills = CopySkills(screen.root);
  if (out.skills.empty()) {
    out.unavailable_reason =
        "The native skill controller has not populated its upgrade tree.";
    return out;
  }
  for (NativeUiRow& row : out.skills) row.purchasable = row.enabled;
  out.available = true;
  out.available_points = ReadText(screen.root,
      {"G_AvailableUpgrades/T_Upgrades_Val", "T_Upgrades_Val"});
  out.selected_name = ReadText(screen.root,
      {"G_SkillDesc/T_SkillName", "T_SkillName"});
  out.selected_level = ReadText(screen.root,
      {"G_SkillDesc/T_SkillLevel_Val", "T_SkillLevel_Val"});
  out.description = ReadText(screen.root,
      {"G_SkillDesc/T_SkillDesc", "G_SkillDesc/T_SkillDesc_SD",
       "T_SkillDesc"});
  out.learn_control_enabled = Enabled(screen.root,
      {"G_MenuButtons/B_Select"});
  out.view_control_enabled = Enabled(screen.root,
      {"G_MenuButtons/B_ViewSkill"});
  out.reskill_control_enabled = Enabled(screen.root,
      {"G_MenuButtons/B_Reskill"});
  IUIElement* confirm = FindExact(screen.root, {"G_ConfirmAddSkill"});
  IUIElement* confirm_sd = FindExact(screen.root, {"G_ConfirmAddSkill_SD"});
  out.confirmation_visible =
      (confirm && TheEngine().IsVisible(confirm)) ||
      (confirm_sd && TheEngine().IsVisible(confirm_sd));
  return out;
}

void FingerprintBool(std::string* out, bool value) {
  out->push_back(value ? '1' : '0');
}

void FingerprintInt(std::string* out, int value) {
  *out += std::to_string(value);
  out->push_back(';');
}

void FingerprintText(std::string* out, const std::string& value) {
  *out += std::to_string(value.size());
  out->push_back(':');
  out->append(value);
  out->push_back(';');
}

void FingerprintRows(std::string* out, const std::vector<NativeUiRow>& rows) {
  FingerprintInt(out, static_cast<int>(rows.size()));
  for (const NativeUiRow& row : rows) {
    FingerprintText(out, row.id);
    FingerprintText(out, row.icon_texture);
    FingerprintInt(out, row.item_category);
    FingerprintBool(out, row.selected);
    FingerprintBool(out, row.enabled);
    FingerprintBool(out, row.purchasable);
    FingerprintInt(out, row.group);
    FingerprintInt(out, row.tier);
    FingerprintInt(out, row.native_ordinal);
    FingerprintBool(out, row.has_native_position);
    FingerprintInt(out, static_cast<int>(std::lround(row.native_x * 10.0f)));
    FingerprintInt(out, static_cast<int>(std::lround(row.native_y * 10.0f)));
    FingerprintInt(out, static_cast<int>(row.text.size()));
    for (const std::string& value : row.text) FingerprintText(out, value);
  }
}

void FingerprintRadarVisual(std::string* out,
                            const HudRadarVisual& visual) {
  FingerprintInt(out, static_cast<int>(std::lround(visual.x)));
  FingerprintInt(out, static_cast<int>(std::lround(visual.y)));
  FingerprintInt(out, static_cast<int>(std::lround(visual.width)));
  FingerprintInt(out, static_cast<int>(std::lround(visual.height)));
  FingerprintInt(out, static_cast<int>(std::lround(visual.scale_x * 100.0f)));
  FingerprintInt(out, static_cast<int>(std::lround(visual.scale_y * 100.0f)));
  FingerprintText(out, visual.texture);
  FingerprintText(out, visual.label);
  FingerprintBool(out, visual.up);
}

void FingerprintRadar(std::string* out, const HudRadarSnapshot& radar) {
  FingerprintBool(out, radar.available);
  if (!radar.available) return;
  FingerprintRadarVisual(out, radar.player);
  FingerprintInt(out, static_cast<int>(radar.path.size()));
  for (const HudRadarVisual& visual : radar.path)
    FingerprintRadarVisual(out, visual);
  FingerprintInt(out, static_cast<int>(radar.markers.size()));
  for (const HudRadarVisual& visual : radar.markers)
    FingerprintRadarVisual(out, visual);
}

std::string Fingerprint(const InventorySnapshot& inventory,
                        const GearSnapshot& gear,
                        const MissionsSnapshot& missions,
                        const SkillsSnapshot& skills) {
  std::string out;
  out.reserve(4096);
  FingerprintBool(&out, inventory.available);
  FingerprintText(&out, inventory.unavailable_reason);
  FingerprintText(&out, inventory.selected_name);
  FingerprintText(&out, inventory.description);
  FingerprintText(&out, inventory.price);
  FingerprintText(&out, inventory.filter);
  FingerprintRows(&out, inventory.items);
  FingerprintBool(&out, inventory.equip_control_enabled);
  FingerprintBool(&out, inventory.drop_control_enabled);
  FingerprintBool(&out, inventory.compare_control_enabled);
  FingerprintBool(&out, inventory.inspect_control_enabled);

  FingerprintBool(&out, gear.available);
  FingerprintText(&out, gear.unavailable_reason);
  FingerprintRows(&out, gear.quick_slots);

  FingerprintBool(&out, missions.available);
  FingerprintText(&out, missions.unavailable_reason);
  FingerprintText(&out, missions.selected_name);
  FingerprintText(&out, missions.reward);
  FingerprintText(&out, missions.description);
  FingerprintRows(&out, missions.quests);
  FingerprintRows(&out, missions.objectives);
  FingerprintBool(&out, missions.track_control_enabled);
  FingerprintBool(&out, missions.details_control_enabled);

  FingerprintBool(&out, skills.available);
  FingerprintText(&out, skills.unavailable_reason);
  FingerprintText(&out, skills.available_points);
  FingerprintText(&out, skills.selected_name);
  FingerprintText(&out, skills.selected_level);
  FingerprintText(&out, skills.description);
  FingerprintRows(&out, skills.skills);
  FingerprintBool(&out, skills.learn_control_enabled);
  FingerprintBool(&out, skills.view_control_enabled);
  FingerprintBool(&out, skills.reskill_control_enabled);
  FingerprintBool(&out, skills.confirmation_visible);
  return out;
}

bool ControlUsable(IUIElement* control) {
  Engine& engine = TheEngine();
  return control && engine.IsUiControl(control) && engine.IsVisible(control) &&
         engine.IsControlEnabled(control);
}

bool SelectOnlyIfEnabled(IUIElement* control) {
  return ControlUsable(control) && TheEngine().SelectControl(control);
}

bool ActivateButtonIfEnabled(IUIElement* control) {
  if (!ControlUsable(control)) return false;
  Engine& engine = TheEngine();
  // Native menus focus a button before routing its click. Mirroring that order
  // keeps controller-owned selection/pending state coherent.
  if (!engine.SelectControl(control)) return false;
  return engine.ClickControl(control);
}

bool SameRowIdentity(const NativeUiRow& expected, IUIElement* current,
                     int group = -1, int tier = -1,
                     int native_ordinal = -1) {
  if (!current) return false;
  const NativeUiRow now =
      CopyControlRow(current, group, tier, native_ordinal);
  return now.id == expected.id && now.text == expected.text &&
         now.group == expected.group && now.tier == expected.tier &&
         now.native_ordinal == expected.native_ordinal;
}

bool ActivatePath(ILevel* level, const char* screen_name,
                  std::initializer_list<const char*> paths) {
  const ScreenRef screen = NativeScreen(level, screen_name);
  if (!screen.root) return false;
  return ActivateButtonIfEnabled(FindExact(screen.root, paths));
}

bool SelectListIndex(ILevel* level, const char* screen_name,
                     std::initializer_list<const char*> list_paths,
                     std::size_t index, const NativeUiRow& expected) {
  const ScreenRef screen = NativeScreen(level, screen_name);
  if (!screen.root) return false;
  IUIElement* list = FindExact(screen.root, list_paths);
  if (!list) return false;
  const std::vector<IUIElement*> rows = VisibleListControls(list);
  if (index >= rows.size()) return false;
  if (!SameRowIdentity(expected, rows[index], -1, -1,
                       static_cast<int>(index))) return false;
  return SelectOnlyIfEnabled(rows[index]);
}

}  // namespace

void ObserveNativeMenuRegistration(ILevel* host, int screen_id,
                                   const char* script_name) {
  if (!host || screen_id < 0 || !script_name || !*script_name) return;
  ILevel* previous = g_native_menu_host.exchange(host, std::memory_order_acq_rel);
  if (previous != host) {
    DIS_INFO("[native] authored menu host = %p via %s (%d)", host,
             script_name, screen_id);
  }
}

void RetireNativeMenuHost(ILevel* host) {
  if (!host) return;
  ILevel* expected = host;
  if (g_native_menu_host.compare_exchange_strong(
          expected, nullptr, std::memory_order_acq_rel)) {
    DIS_INFO("[native] authored menu host %p retired", host);
  }
}

#ifdef DIS_UI_PREVIEW
void NativeUi::SetPreviewData() {
  inventory_ = {};
  gear_ = {};
  skills_ = {};
  hud_ = {};
  inventory_.available = true;
  inventory_.filter = "ALL ITEMS";
  static const char* const items[][3] = {
      {"BRUTAL WRENCH", "weapon_type_blunt", "72 / 100"},
      {"AUTO RIFLE", "icon_item_rifle", "30 / 90"},
      {"MEDKIT", "icon_item_medkit", "2"},
      {"ENERGY DRINK", "inv_wpn_bottle", "4"},
      {"MOLOTOV", "icon_item_molotov", "3"},
      {"MACHETE", "weapon_type_sharp", "91 / 100"},
      {"PISTOL AMMO", "icon_item_ammo", "48"},
      {"DEODORANT BOMB", "icon_item_grenade", "2"},
      {"HEAVY HAMMER", "weapon_type_blunt", "63 / 100"},
      {"BANDAGES", "icon_item_health", "5"},
  };
  for (const auto& item : items) {
    NativeUiRow row;
    row.text = {item[0], item[2]};
    row.id = item[0];
    row.icon_texture = item[1];
    row.enabled = true;
    inventory_.items.push_back(std::move(row));
  }
  inventory_.items.front().selected = true;
  inventory_.selected_name = items[0][0];
  inventory_.description =
      "A dependable improvised weapon. Preview data exercises the live page layout only.";
  inventory_.equip_control_enabled = true;
  inventory_.drop_control_enabled = true;

  gear_.available = true;
  for (int i = 0; i < 8; ++i) {
    NativeUiRow row;
    if (i < static_cast<int>(inventory_.items.size())) row = inventory_.items[i];
    row.group = i;
    row.enabled = i < 6;
    gear_.quick_slots.push_back(std::move(row));
  }

  skills_.available = true;
  skills_.available_points = "2";
  static const char* const skill_names[3][7][3] = {
      {{"Bloodrage", nullptr, nullptr},
       {"Grim Inspiration", "Furious Gain", "Blood Trance"},
       {"Furious Charge", nullptr, nullptr},
       {"Longer Fury", "Group Fury", "Rage Gain"},
       {"Ultimate Fury", nullptr, nullptr},
       {nullptr, nullptr, nullptr},
       {nullptr, nullptr, nullptr}},
      {{"Heavy Hitter", nullptr, nullptr},
       {"Sharp Apprentice", "Blunt Training", "Firearm Training"},
       {"Maintenance", "Critical Hit", "Stamina Control"},
       {"Weapon Expert", nullptr, nullptr},
       {"Bleeding", "Knockdown", "Fire Damage"},
       {"Finishing Blow", "Durability", "Weapon Master"},
       {"Combat Mastery", nullptr, nullptr}},
      {{"Backpacker", nullptr, nullptr},
       {"Lockpick", "Medical Efficiency", "Conditioning"},
       {"Deeper Pockets", "Regeneration", "Treasure Hunter"},
       {"Survivalist", nullptr, nullptr},
       {"Appraiser", "Hardened", "Elemental Resistance"},
       {"Scavenger", "Medic", "Endurance"},
       {"Survival Expert", nullptr, nullptr}},
  };
  for (int tree = 0; tree < 3; ++tree) {
    for (int tier = 0; tier < 7; ++tier) {
      for (int branch = 0; branch < 3; ++branch) {
        if (!skill_names[tree][tier][branch]) continue;
        NativeUiRow row;
        row.id = skill_names[tree][tier][branch];
        row.text = {row.id, tier < 3 ? "1/3" : "0/3"};
        row.icon_texture = tree == 0 ? "skill_fury" :
            (tree == 1 ? "skill_combat" : "skill_survival");
        row.group = tree;
        row.tier = tier;
        row.native_x = branch == 0 && !skill_names[tree][tier][1]
            ? 112.0f : 8.0f + branch * 104.0f;
        row.native_y = static_cast<float>(tier * 63);
        row.has_native_position = true;
        row.enabled = tier < 4;
        row.purchasable = row.enabled;
        row.selected = tree == 1 && tier == 2 && branch == 0;
        skills_.skills.push_back(std::move(row));
      }
    }
  }
  skills_.selected_name = "Maintenance";
  skills_.selected_level = "LEVEL 1 / 3";
  skills_.description =
      "Weapons lose durability more slowly. Select Learn / Upgrade to use the game's real purchase controller.";
  skills_.learn_control_enabled = true;

  hud_.available = true;
  hud_.equipped_name = "BRUTAL WRENCH";
  hud_.icon_texture = "weapon_type_blunt";
  hud_.durability = "68 / 75";
  hud_.item_stats = {"312.6", "245.0", "68 / 75", "92"};
  fingerprint_ = Fingerprint(inventory_, gear_, missions_, skills_);
  hud_fingerprint_ = "preview";
  ++revision_;
  ++inventory_revision_;
  ++missions_revision_;
  ++skills_revision_;
}
#endif

void NativeUi::CloseInteractive(ILevel* level) {
  if (interactive_screen_id_ < 0) {
    interactive_domain_ = NativeUiDomain::kAll;
    interactive_ready_ = false;
    close_when_ready_ = false;
    activation_polls_ = 0;
    pending_quick_slot_ = -1;
    pending_quick_slot_wait_ = false;
    return;
  }

  Engine& engine = TheEngine();
  const int closing_id = interactive_screen_id_;
  const NativeUiDomain closing_domain = interactive_domain_;
  ILevel* const ui_level = NativeMenuHost(level);
  // Only touch the root while it still belongs to the same proven level.
  // Teardown calls Reset after the level pointer has been retired, so stale
  // roots are abandoned rather than dereferenced.
  if (level && level == level_ && ui_level && engine.ready() &&
      engine.IsUIManagerVisible(ui_level)) {
    if (IUIElement* root = engine.FindScreen(ui_level, closing_id)) {
      engine.SetOpacity(root, 1.0f);
    }
    engine.HideScreenById(ui_level, closing_id);
  }

  interactive_screen_id_ = -1;
  interactive_domain_ = NativeUiDomain::kAll;
  interactive_ready_ = false;
  close_when_ready_ = false;
  activation_polls_ = 0;
  pending_quick_slot_ = -1;
  pending_quick_slot_wait_ = false;
  DIS_INFO("[native] companion screen %d closed", closing_id);

  // Hide destroys controller-owned row records. Never leave a snapshot that
  // can be used to call one of those records. Quick-slot display values and
  // fixed B_Slot ordinals are owned copies, so retain them as a read-only
  // Status-page cache; an action always opens and reacquires the controller.
  if (closing_domain == NativeUiDomain::kInventory) {
    inventory_ = {};
    inventory_.unavailable_reason = "Open the Inventory page to refresh.";
    gear_.available = false;
    gear_.unavailable_reason = "Cached quick-slot display.";
  } else if (closing_domain == NativeUiDomain::kSkills) {
    skills_ = {};
    skills_.unavailable_reason = "Open the Skills page to refresh.";
  }
  fingerprint_ = Fingerprint(inventory_, gear_, missions_, skills_);
  ++revision_;
  if (closing_domain == NativeUiDomain::kInventory) ++inventory_revision_;
  if (closing_domain == NativeUiDomain::kSkills) ++skills_revision_;
}

bool NativeUi::AdvanceInteractive(ILevel* level) {
  if (interactive_screen_id_ < 0 || !level || level != level_) return false;
  Engine& engine = TheEngine();
  ILevel* const ui_level = NativeMenuHost(level);
  if (!ui_level || !engine.ready() || !engine.IsUIManagerVisible(ui_level)) {
    interactive_ready_ = false;
    return false;
  }

  if (!engine.IsScreenLoaded(ui_level, interactive_screen_id_)) {
    interactive_ready_ = false;
    if (activation_polls_ < 0x3fffffff) ++activation_polls_;
    // The gamedll menu request may finish on a later UI tick. Retry sparsely
    // if that request was dropped while the level was still settling; never
    // issue it every Present like the old failing LoadUIScreen path did.
    if (activation_polls_ == 8 || (activation_polls_ > 8 &&
                                   (activation_polls_ % 32) == 0)) {
      RequestAuthoredMenu(ui_level, interactive_screen_id_);
    }
    return false;
  }

  IUIElement* root = engine.FindScreen(ui_level, interactive_screen_id_);
  if (!root) {
    interactive_ready_ = false;
    if (activation_polls_ < 0x3fffffff) ++activation_polls_;
    return false;
  }

  // ShowUIScreen is a request, not a documented synchronous constructor.
  // Keep the shell suppressed while its controller finishes on a later UI
  // tick. Requiring visibility in the same call made a deferred Show get
  // hidden immediately, so it could never populate its rows.
  engine.SetOpacity(root, 0.0f);
  if (!engine.IsScreenVisible(ui_level, interactive_screen_id_)) {
    interactive_ready_ = false;
    if (activation_polls_ < 0x3fffffff) ++activation_polls_;
    // A retry is deliberately sparse: enough to recover from a dropped show
    // request without queueing one every Present/UI update.
    if (activation_polls_ == 8 || (activation_polls_ > 8 &&
                                   (activation_polls_ % 32) == 0)) {
      RequestAuthoredMenu(ui_level, interactive_screen_id_);
    }
    return false;
  }

  if (!interactive_ready_) {
    DIS_INFO("[native] companion screen %d controller is live",
             interactive_screen_id_);
  }
  interactive_ready_ = true;
  activation_polls_ = 0;
  // A Show handler may restore animation properties; keep the native screen
  // render-suppressed on every poll, not only on the requesting frame.
  engine.SetOpacity(root, 0.0f);
  return true;
}

void NativeUi::Update(ILevel* level, NativeUiDomain domain) {
  const bool level_changed = level != level_;
  level_ = level;

  if (level_changed) {
    gameplay::Reset();
    inventory_ = {};
    gear_ = {};
    missions_ = {};
    skills_ = {};
    interactive_screen_id_ = -1;
    interactive_domain_ = NativeUiDomain::kAll;
    interactive_ready_ = false;
    close_when_ready_ = false;
    activation_polls_ = 0;
    pending_quick_slot_ = -1;
    pending_quick_slot_wait_ = false;
  }

  const bool controller_ready = AdvanceInteractive(level);

  InventorySnapshot inventory = inventory_;
  GearSnapshot gear = gear_;
  MissionsSnapshot missions = missions_;
  SkillsSnapshot skills = skills_;
  if (domain == NativeUiDomain::kAll || domain == NativeUiDomain::kInventory) {
    if (!gameplay::SampleInventory(&inventory, &gear)) {
      const ScreenRef inventory_screen =
          NativeScreen(level, kInventoryScreen);
      inventory = BuildInventory(inventory_screen);
      gear = BuildGear(inventory_screen);
    }
  }
  if (domain == NativeUiDomain::kAll || domain == NativeUiDomain::kMissions) {
    missions = BuildMissions(NativeScreen(level, kMissionsScreen));
  }
  if (domain == NativeUiDomain::kAll || domain == NativeUiDomain::kSkills) {
    if (!gameplay::SampleSkills(TheGameState().vitals(), &skills)) {
      skills = BuildSkills(NativeScreen(level, kSkillsScreen));
    }
  }

  // A Status-page quick-slot tap can request MenuInventory before its
  // controller becomes visible. Complete that semantic action only after the
  // actual B_Slot control exists, then let the close path discard every
  // controller-owned pointer. No input event or guessed gameplay field is
  // involved.
  if (controller_ready && pending_quick_slot_ >= 0 &&
      pending_quick_slot_wait_) {
    // Even when IsUIScreenVisible flips synchronously, its controller data
    // associations are not guaranteed to have completed in the requesting
    // Present. Require a later poll before touching B_SlotN.
    pending_quick_slot_wait_ = false;
  } else if (controller_ready && pending_quick_slot_ >= 0 &&
      interactive_domain_ == NativeUiDomain::kInventory) {
    const ScreenRef screen = NativeScreen(level, kInventoryScreen);
    IUIElement* group = screen.root ? FindKnown(screen.root,
        {"G_Content/G_Group/G_QuickSlots", "G_QuickSlots"}) : nullptr;
    const std::string id = "B_Slot" + std::to_string(pending_quick_slot_);
    IUIElement* control = group ? FindExact(group, {id.c_str()}) : nullptr;
    if (control) {
      const bool activated = ActivateButtonIfEnabled(control);
      DIS_INFO("[native] deferred quick slot %d %s",
               pending_quick_slot_, activated ? "activated" : "was unavailable");
      pending_quick_slot_ = -1;
      pending_quick_slot_wait_ = false;
      // Preserve the caller's ownership decision. A Status-page one-shot set
      // close_when_ready_ when it queued the action; an Inventory-page action
      // must leave that page's live controller open for subsequent taps.
      if (activated) {
        const ScreenRef refreshed = NativeScreen(level, kInventoryScreen);
        inventory = BuildInventory(refreshed);
        gear = BuildGear(refreshed);
      }
    }
  }

  std::string fingerprint = Fingerprint(inventory, gear, missions, skills);
  if (level_changed || fingerprint != fingerprint_) {
    inventory_ = std::move(inventory);
    gear_ = std::move(gear);
    missions_ = std::move(missions);
    skills_ = std::move(skills);
    fingerprint_ = std::move(fingerprint);
    ++revision_;
    if (domain == NativeUiDomain::kAll ||
        domain == NativeUiDomain::kInventory) ++inventory_revision_;
    if (domain == NativeUiDomain::kAll ||
        domain == NativeUiDomain::kMissions) ++missions_revision_;
    if (domain == NativeUiDomain::kAll ||
        domain == NativeUiDomain::kSkills) ++skills_revision_;
    DIS_INFO("[native] inventory=%d/%d gear=%d/%d missions=%d/%d objectives=%d"
             " skills=%d/%d",
             inventory_.available ? 1 : 0,
             static_cast<int>(inventory_.items.size()),
             gear_.available ? 1 : 0,
             static_cast<int>(gear_.quick_slots.size()),
             missions_.available ? 1 : 0,
             static_cast<int>(missions_.quests.size()),
             static_cast<int>(missions_.objectives.size()),
             skills_.available ? 1 : 0,
             static_cast<int>(skills_.skills.size()));
    if (!inventory_.available && !inventory_.unavailable_reason.empty()) {
      DIS_WARN("[native] inventory: %s", inventory_.unavailable_reason.c_str());
    }
    if (!missions_.available && !missions_.unavailable_reason.empty()) {
      DIS_WARN("[native] missions: %s", missions_.unavailable_reason.c_str());
    }
    if (!skills_.available && !skills_.unavailable_reason.empty()) {
      DIS_WARN("[native] skills: %s", skills_.unavailable_reason.c_str());
    }
  }

  // A close requested while Show was still pending is honored only after one
  // safe snapshot. This is what lets the default Status-page cache prime on a
  // deferred UI implementation without exposing stale native controls.
  // A queued quick-slot action deliberately waits one additional UI poll
  // after visibility. Do not let the one-shot close request tear down the
  // controller during that settling poll.
  if (controller_ready && close_when_ready_ && pending_quick_slot_ < 0) {
    CloseInteractive(level);
  }
}

void NativeUi::UpdateHud(ILevel* level) {
  if (level != level_) {
    gameplay::Reset();
    level_ = level;
    inventory_ = {};
    gear_ = {};
    missions_ = {};
    skills_ = {};
    fingerprint_ = Fingerprint(inventory_, gear_, missions_, skills_);
    hud_ = {};
    hud_fingerprint_.clear();
    // The old controller belongs to the retired level. Do not try to restore
    // or hide it through the replacement level's UI manager.
    interactive_screen_id_ = -1;
    interactive_domain_ = NativeUiDomain::kAll;
    interactive_ready_ = false;
    close_when_ready_ = false;
    activation_polls_ = 0;
    pending_quick_slot_ = -1;
    pending_quick_slot_wait_ = false;
  }
  // Status-page cache priming and one-shot quick-slot actions can leave a
  // Show request pending without a page-scoped Update caller. The HUD sample
  // is already a low-frequency game-thread callback, so use it to advance that
  // request across UI ticks before reading the always-live HUD.
  if (interactive_screen_id_ >= 0 &&
      interactive_domain_ != NativeUiDomain::kAll) {
    Update(level, interactive_domain_);
  }
  HudSnapshot next = BuildHud(level);
  // Inventory, quick slots and held-item identity live on PlayerDI and are
  // available even when MenuInventory has never been opened. Refresh that
  // small snapshot alongside the 10 Hz HUD sample so the default Stats page
  // is complete immediately after the player becomes live.
  InventorySnapshot direct_inventory = inventory_;
  GearSnapshot direct_gear = gear_;
  if (gameplay::SampleInventory(&direct_inventory, &direct_gear, &next)) {
    std::string direct_fingerprint =
        Fingerprint(direct_inventory, direct_gear, missions_, skills_);
    if (direct_fingerprint != fingerprint_) {
      inventory_ = std::move(direct_inventory);
      gear_ = std::move(direct_gear);
      fingerprint_ = std::move(direct_fingerprint);
      ++revision_;
      ++inventory_revision_;
    }
  }
  std::string fingerprint;
  FingerprintBool(&fingerprint, next.available);
  FingerprintText(&fingerprint, next.equipped_name);
  FingerprintText(&fingerprint, next.icon_texture);
  FingerprintText(&fingerprint, next.xp);
  FingerprintText(&fingerprint, next.clip_ammo);
  FingerprintText(&fingerprint, next.total_ammo);
  FingerprintText(&fingerprint, next.held_quantity);
  FingerprintText(&fingerprint, next.durability);
  for (const std::string& value : next.item_stats)
    FingerprintText(&fingerprint, value);
  FingerprintRadar(&fingerprint, next.radar);
  if (fingerprint == hud_fingerprint_) return;
  hud_ = std::move(next);
  hud_fingerprint_ = std::move(fingerprint);
  ++revision_;
  DIS_INFO("[native] HUD feed=%d weapon='%s' ammo='%s/%s' quantity='%s'",
           hud_.available ? 1 : 0, hud_.equipped_name.c_str(),
           hud_.clip_ammo.c_str(), hud_.total_ammo.c_str(),
           hud_.held_quantity.c_str());
}

bool NativeUi::SetInteractiveDomain(ILevel* level, NativeUiDomain domain) {
  // On the verified build Inventory and Skills are gameplay-owned data, not
  // hidden-menu data. Keep the public page-lifetime contract while avoiding
  // construction of a transparent XUI controller that can pause, flash or
  // destabilize save loading.
  if (gameplay::Supported() && interactive_screen_id_ < 0) {
    level_ = level;
    if (domain == NativeUiDomain::kInventory ||
        domain == NativeUiDomain::kSkills) {
      Update(level, domain);
    }
    return true;
  }
  Engine& engine = TheEngine();
  ILevel* const ui_level = NativeMenuHost(level);
  if (!level || !ui_level || !engine.ready() ||
      !engine.IsUIManagerVisible(ui_level)) {
    return false;
  }

  const char* desired_name = nullptr;
  if (domain == NativeUiDomain::kInventory) desired_name = kInventoryScreen;
  if (domain == NativeUiDomain::kSkills) desired_name = kSkillsScreen;

  const int desired_id = desired_name
      ? RegisteredScreenId(level, desired_name) : -1;
  if (desired_name && desired_id < 0) return false;

  // A level replacement invalidates every prior screen pointer/id ownership
  // relationship. Abandon state without trying to call through the new
  // level's UI manager using an id from the old one.
  if (level_ && level_ != level) {
    interactive_screen_id_ = -1;
    interactive_domain_ = NativeUiDomain::kAll;
    interactive_ready_ = false;
    close_when_ready_ = false;
    activation_polls_ = 0;
    pending_quick_slot_ = -1;
    pending_quick_slot_wait_ = false;
  }

  if (desired_name && interactive_screen_id_ >= 0 &&
      (level != level_ || interactive_screen_id_ != desired_id)) {
    CloseInteractive(level_ == level ? level : nullptr);
  }

  level_ = level;
  if (!desired_name) {
    if (interactive_screen_id_ < 0) return true;
    // Show may not have reached the controller yet. Keep the root at zero
    // opacity until Update/UpdateHud can take one valid snapshot, then close
    // it normally. Even when visibility became true between two calls, route
    // through Update before closing so cache priming never loses that frame.
    close_when_ready_ = true;
    return true;
  }

  if (interactive_screen_id_ == desired_id &&
      interactive_domain_ == domain) {
    close_when_ready_ = false;
    AdvanceInteractive(level);
    Update(level, domain);
    return true;
  }

  interactive_screen_id_ = desired_id;
  interactive_domain_ = domain;
  interactive_ready_ = engine.IsScreenVisible(ui_level, desired_id);
  close_when_ready_ = false;
  activation_polls_ = 0;
  pending_quick_slot_ = -1;
  pending_quick_slot_wait_ = false;

  if (!interactive_ready_ && !RequestAuthoredMenu(ui_level, desired_id)) {
    DIS_WARN("[native] could not request companion %s (%d)", desired_name,
             desired_id);
    interactive_screen_id_ = -1;
    interactive_domain_ = NativeUiDomain::kAll;
    return false;
  }

  // The native request is normally synchronous through XUI construction, so
  // suppress its root before the next Present. If this build defers creation,
  // AdvanceInteractive applies the same suppression as soon as the root
  // exists and before it is considered ready for traversal.
  if (IUIElement* root = engine.FindScreen(ui_level, desired_id)) {
    engine.SetOpacity(root, 0.0f);
  }

  DIS_INFO("[native] companion %s requested on screen %d%s", desired_name,
           desired_id, interactive_ready_ ? " (already visible)" : "");
  AdvanceInteractive(level);
  Update(level, domain);
  return true;
}

void NativeUi::Reset() {
  gameplay::Reset();
  level_ = nullptr;
  inventory_ = {};
  gear_ = {};
  missions_ = {};
  skills_ = {};
  hud_ = {};
  inventory_.unavailable_reason = "No active level.";
  gear_.unavailable_reason = "No active level.";
  missions_.unavailable_reason = "No active level.";
  skills_.unavailable_reason = "No active level.";
  fingerprint_ = Fingerprint(inventory_, gear_, missions_, skills_);
  hud_fingerprint_.clear();
  interactive_screen_id_ = -1;
  interactive_domain_ = NativeUiDomain::kAll;
  interactive_ready_ = false;
  close_when_ready_ = false;
  activation_polls_ = 0;
  pending_quick_slot_ = -1;
  pending_quick_slot_wait_ = false;
  ++revision_;
  ++inventory_revision_;
  ++missions_revision_;
  ++skills_revision_;
}

bool NativeUi::SelectInventoryItem(std::size_t index) {
  if (gameplay::Supported()) {
    const bool done = gameplay::SelectInventoryItem(index);
    if (done) Update(level_, NativeUiDomain::kInventory);
    return done;
  }
  if (index >= inventory_.items.size()) return false;
  const bool done = SelectListIndex(level_, kInventoryScreen,
      {"G_Content/G_Group/L_List"}, index, inventory_.items[index]);
  if (done) Update(level_, NativeUiDomain::kInventory);
  return done;
}

bool NativeUi::AssignInventoryItemToQuickSlot(std::size_t inventory_index,
                                              std::size_t quick_slot) {
  // No hidden-XUI fallback is used here. Only the version/signature-gated
  // gameplay bridge knows the native assignment ABI and validates current
  // ownership before the mutating call.
  if (!gameplay::Supported()) return false;
  const bool done = gameplay::AssignInventoryItemToQuickSlot(
      inventory_index, quick_slot);
  if (done) {
    Update(level_, NativeUiDomain::kInventory);
    UpdateHud(level_);
  }
  return done;
}

bool NativeUi::SelectGearSlot(std::size_t index) {
  if (gameplay::Supported()) {
    const bool done = gameplay::SelectGearSlot(index);
    if (done) Update(level_, NativeUiDomain::kInventory);
    return done;
  }
  if (index >= 9) return false;
  const ScreenRef screen = NativeScreen(level_, kInventoryScreen);
  if (!screen.root) return false;
  IUIElement* group = FindKnown(screen.root,
      {"G_Content/G_Group/G_QuickSlots", "G_QuickSlots"});
  if (!group) return false;
  const std::string id = "B_Slot" + std::to_string(index);
  const bool done =
      ActivateButtonIfEnabled(FindExact(group, {id.c_str()}));
  if (done) Update(level_, NativeUiDomain::kInventory);
  return done;
}

bool NativeUi::ActivateQuickSlot(std::size_t index) {
  if (gameplay::Supported()) {
    const bool done = gameplay::ActivateQuickSlot(index);
    if (done) {
      Update(level_, NativeUiDomain::kInventory);
      UpdateHud(level_);
    }
    return done;
  }
  if (index >= 9 || !level_) return false;
  const bool inventory_already_open =
      interactive_screen_id_ >= 0 &&
      interactive_domain_ == NativeUiDomain::kInventory;
  if (inventory_already_open && interactive_ready_) {
    return SelectGearSlot(index);
  }
  if (!inventory_already_open &&
      !SetInteractiveDomain(level_, NativeUiDomain::kInventory)) {
    return false;
  }

  // A newly requested screen must survive at least one later UI poll even if
  // IsUIScreenVisible happened to flip synchronously. Visibility only proves
  // the authored shell is present; it does not prove its controller data
  // associations were bound before this Present. Queue the fixed B_Slot
  // ordinal and let UpdateHud/Update finish and close it safely.
  pending_quick_slot_ = static_cast<int>(index);
  pending_quick_slot_wait_ = true;
  close_when_ready_ = !inventory_already_open;
  return true;
}

bool NativeUi::SelectMission(std::size_t index) {
  if (index >= missions_.quests.size()) return false;
  const bool done = SelectListIndex(level_, kMissionsScreen,
      {"G_Content/G_Left/G_AlignGroup/L_Quests"}, index,
      missions_.quests[index]);
  if (done) Update(level_, NativeUiDomain::kMissions);
  return done;
}

bool NativeUi::SelectSkill(std::size_t index) {
  if (gameplay::Supported()) {
    const bool done = gameplay::SelectSkill(index);
    if (done) Update(level_, NativeUiDomain::kSkills);
    return done;
  }
  if (index >= skills_.skills.size()) return false;
  const ScreenRef screen = NativeScreen(level_, kSkillsScreen);
  if (!screen.root) return false;
  const std::vector<IUIElement*> controls = SkillControls(screen.root);
  if (index >= controls.size()) return false;
  const NativeUiRow& expected = skills_.skills[index];
  if (!SameRowIdentity(expected, controls[index], expected.group,
                       expected.tier, expected.native_ordinal)) {
    return false;
  }
  const bool done = SelectOnlyIfEnabled(controls[index]);
  if (done) Update(level_, NativeUiDomain::kSkills);
  return done;
}

bool NativeUi::SelectInventoryAction(InventoryNativeAction action) {
  if (gameplay::Supported()) {
    const bool done = gameplay::InventoryAction(action);
    if (done) {
      Update(level_, NativeUiDomain::kInventory);
      UpdateHud(level_);
    }
    return done;
  }
  const char* id = nullptr;
  switch (action) {
    case InventoryNativeAction::kEquip: id = "B_Equip"; break;
    case InventoryNativeAction::kDrop: id = "B_Drop"; break;
    case InventoryNativeAction::kCompare: id = "B_Compare"; break;
    case InventoryNativeAction::kInspect: id = "B_Inspect"; break;
    case InventoryNativeAction::kCollectableDetails:
      id = "B_CollectableDetails";
      break;
    case InventoryNativeAction::kFilter: id = "B_Filter"; break;
    case InventoryNativeAction::kBack: id = "B_Cancel"; break;
  }
  if (!id) return false;
  const std::string path = std::string("G_Content/G_MenuButtons/") + id;
  const bool done = ActivatePath(level_, kInventoryScreen, {path.c_str()});
  if (done) Update(level_, NativeUiDomain::kInventory);
  return done;
}

bool NativeUi::SelectMissionAction(MissionNativeAction action) {
  const char* id = nullptr;
  switch (action) {
    case MissionNativeAction::kTrack: id = "B_TrackQuest"; break;
    case MissionNativeAction::kDetails: id = "B_ButtonDetails"; break;
    case MissionNativeAction::kBack: id = "B_ButtonBack"; break;
  }
  if (!id) return false;
  const std::string path = std::string("G_Content/G_Menu/") + id;
  const bool done = ActivatePath(level_, kMissionsScreen, {path.c_str()});
  if (done) Update(level_, NativeUiDomain::kMissions);
  return done;
}

bool NativeUi::SelectSkillAction(SkillNativeAction action) {
  if (gameplay::Supported()) {
    const bool done = gameplay::SkillAction(action);
    if (done) Update(level_, NativeUiDomain::kSkills);
    return done;
  }
  const char* id = nullptr;
  bool confirmation = false;
  switch (action) {
    case SkillNativeAction::kLearn: id = "B_Select"; break;
    case SkillNativeAction::kView: id = "B_ViewSkill"; break;
    case SkillNativeAction::kReskill: id = "B_Reskill"; break;
    case SkillNativeAction::kBack: id = "B_Back"; break;
    case SkillNativeAction::kConfirmYes:
      id = "ConfirmButtonYes";
      confirmation = true;
      break;
    case SkillNativeAction::kConfirmNo:
      id = "ConfirmButtonNo";
      confirmation = true;
      break;
    case SkillNativeAction::kConfirmOk:
      id = "ConfirmButtonOk";
      confirmation = true;
      break;
  }
  if (!id) return false;

  const ScreenRef screen = NativeScreen(level_, kSkillsScreen);
  if (!screen.root) return false;
  IUIElement* control = nullptr;
  if (confirmation) {
    // The standard and SD layouts contain duplicate button ids. Resolve only
    // inside the group that is actually visible so a purchase cannot click a
    // hidden sibling dialog.
    IUIElement* group = FindExact(screen.root, {"G_ConfirmAddSkill"});
    IUIElement* group_sd =
        FindExact(screen.root, {"G_ConfirmAddSkill_SD"});
    IUIElement* visible_group = nullptr;
    if (group && TheEngine().IsVisible(group)) visible_group = group;
    if (group_sd && TheEngine().IsVisible(group_sd)) visible_group = group_sd;
    if (!visible_group) return false;
    control = FindExact(visible_group, {id});
  } else {
    const std::string path = std::string("G_MenuButtons/") + id;
    control = FindExact(screen.root, {path.c_str()});
  }
  const bool done = ActivateButtonIfEnabled(control);
  if (done) Update(level_, NativeUiDomain::kSkills);
  return done;
}

NativeUi& TheNativeUi() {
  static NativeUi native_ui;
  return native_ui;
}

}  // namespace dis
