#include "Layout.h"

#include <algorithm>

#include "Config.h"
#include "Log.h"

namespace dis {
namespace {

Layout g_layout;

// The default routing table.
//
// Three principles decided every line of it.
//
//  - Anything anchored to the world stays on the main screen. A crosshair on
//    the bottom panel is not a crosshair, and a takedown prompt that has left
//    the character it belongs to is worse than no prompt. Reticle, selection
//    boxes, target locking, damage arrows and the objective markers are all in
//    this group, and none of them move.
//  - Anything the player reads rather than glances at becomes a page. The
//    inventory, the augmentation tree, the map, hacking, e-books and every
//    terminal are screens you stop for, and stopping is exactly when a second
//    display earns its keep -- the world stays visible above while you work.
//  - Anything that is a game state rather than an interface element stays put.
//    Menus, loading, cinematics and the death screen own the whole session
//    while they are up; moving them would leave the main display showing a
//    world nobody is in.
struct DefaultRoute { const char* controller; Destination dest; const char* zone; bool touch; };

const DefaultRoute kDefaults[] = {
    // ---- persistent HUD -------------------------------------------------
    {"NsHealthBarMovieController",          Destination::kPanel, "vitals",  false},
    {"NsEnergyBarMovieController",          Destination::kPanel, "vitals",  false},
    {"NsSprintBarMovieController",          Destination::kPanel, "vitals",  false},
    {"NsRadarMovieController",              Destination::kPanel, "sensor",  false},
    {"NsStealthEnhancerMovieController",    Destination::kPanel, "sensor",  false},
    {"NsWeaponIndicatorMovieController",    Destination::kPanel, "weapon",  true},
    {"NsWeaponInfoMovieController",         Destination::kPanel, "weapon",  false},
    {"NsWeaponHeatingMovieController",      Destination::kPanel, "weapon",  false},
    {"NsGrenadesIndicatorMovieController",  Destination::kPanel, "weapon",  false},
    {"NsQuickBarMovieController",           Destination::kPanel, "context", true},
    {"NsContextualActionsMovieController",  Destination::kPanel, "context", false},
    {"NsContextualLegendsMovieController",  Destination::kPanel, "context", false},
    {"NsLogUpdatesMovieController",         Destination::kPanel, "log",     false},
    {"NsPopupManagerMovieController",       Destination::kPanel, "log",     false},
    {"NsInfoCastMovieController",           Destination::kPanel, "log",     false},
    {"NsTutorialMovieController",           Destination::kPanel, "log",     false},
    {"NsBombMovieController",               Destination::kPanel, "log",     false},

    // ---- pages ----------------------------------------------------------
    {"NsInventoryMovieController",          Destination::kPage, "", true},
    {"NsTechTreeMovieController",           Destination::kPage, "", true},
    {"NsAugmentationWheelMovieController",  Destination::kPage, "", true},
    {"NsGameMapMovieController",            Destination::kPage, "", true},
    {"NsMissionLogMovieController",         Destination::kPage, "", true},
    {"NsEmailsMovieController",             Destination::kPage, "", true},
    {"NsNewsReaderMovieController",         Destination::kPage, "", true},
    {"NsMediaLogMovieController",           Destination::kPage, "", true},
    {"NsIReaderMovieController",            Destination::kPage, "", true},
    {"NsHackingMovieController",            Destination::kPage, "", true},
    {"NsShopMovieController",               Destination::kPage, "", true},
    {"NsLootSelectorMovieController",       Destination::kPage, "", true},
    {"NsCodeCollectorMovieController",      Destination::kPage, "", true},
    {"NsSecurityHubMovieController",        Destination::kPage, "", true},
    {"NsDedicatedTerminalMovieController",  Destination::kPage, "", true},
    {"NsKeypadMovieController",             Destination::kPage, "", true},
    {"NsAlarmPanelMovieController",         Destination::kPage, "", true},
    {"NsQuickSelectorMovieController",      Destination::kPage, "", true},
    {"NsMenuWheelMovieController",          Destination::kPage, "", true},
    {"NsSocialAugmentationMovieController", Destination::kPage, "", true},
    {"NsPersonalityTypeMovieController",    Destination::kPage, "", true},

    // ---- world-anchored: never moves -------------------------------------
    {"NsReticleMovieController",                 Destination::kMain, "", false},
    {"NsSelectionBoxMovieController",            Destination::kMain, "", false},
    {"NsSelectionBoxMinimalMovieController",     Destination::kMain, "", false},
    {"NsWorldSpaceSelectionBoxMovieController",  Destination::kMain, "", false},
    {"NsTargetLockingMovieController",           Destination::kMain, "", false},
    {"NsNPCMarkingSystemMovieController",        Destination::kMain, "", false},
    {"NsDamageArrowsMovieController",            Destination::kMain, "", false},
    {"NsObjectivesLocatorMovieController",       Destination::kMain, "", false},
    {"NsOnGunWeaponIndicatorMovieController",    Destination::kMain, "", false},
    {"NsSubtitlesMovieController",               Destination::kMain, "", false},
    {"NsConversationChoiceMovieController",      Destination::kMain, "", false},

    // ---- game states: never moves ----------------------------------------
    {"NsRootMovieController",                    Destination::kMain, "", false},
    {"NsLoadingMovieController",                 Destination::kMain, "", false},
    {"NsMainMenuMovieController",                Destination::kMain, "", false},
    {"NsGameMenuMovieController",                Destination::kMain, "", false},
    {"NsPauseMenuMovieController",               Destination::kMain, "", false},
    {"NsCinematicPauseMenuMovieController",      Destination::kMain, "", false},
    {"NsVideoCinematicPauseMenuMovieController", Destination::kMain, "", false},
    {"NsDeathMenuMovieController",               Destination::kMain, "", false},
    {"NsSaveLoadMovieController",                Destination::kMain, "", false},
    {"NsConfigMovieController",                  Destination::kMain, "", false},
    {"NsVideoMovieController",                   Destination::kMain, "", false},
    {"NsGameIntroMovieController",               Destination::kMain, "", false},
    {"NsEndingMovieController",                  Destination::kMain, "", false},
    {"NsBrieferMovieController",                 Destination::kMain, "", false},
    {"NsGlitchMovieController",                  Destination::kMain, "", false},
    {"NsDefectiveHUDMovieController",            Destination::kMain, "", false},
    {"NsParallaxMovieController",                Destination::kMain, "", false},
    {"NsDebugMenuMovieController",               Destination::kMain, "", false},
    {"NsDLCPopupMovieController",                Destination::kMain, "", false},
};

const char* DestName(Destination d) {
  switch (d) {
    case Destination::kMain: return "main";
    case Destination::kPanel: return "panel";
    case Destination::kPage: return "page";
    case Destination::kHidden: return "hidden";
  }
  return "?";
}

Destination ParseDest(const std::string& text, Destination fallback) {
  if (text == "main") return Destination::kMain;
  if (text == "hidden") return Destination::kHidden;
  if (text == "page") return Destination::kPage;
  if (text.empty()) return fallback;
  return Destination::kPanel;  // anything else names a zone
}

}  // namespace

Layout& TheLayout() { return g_layout; }

void Layout::Build(int panel_width, int panel_height) {
  width_ = panel_width > 0 ? panel_width : 1240;
  height_ = panel_height > 0 ? panel_height : 1080;
  zones_.clear();
  routes_.clear();

  const float w = static_cast<float>(width_);
  const float h = static_cast<float>(height_);

  // About ninety pixels on the Thor and forty-eight in the 620x540 desktop
  // test. It is large enough for touch without consuming the page beneath it.
  header_height_ = Cfg().GetBool("ui.tabs", true)
      ? (std::max)(48, static_cast<int>(h * 0.085f)) : 0;

  // Proportions rather than the pixel constants in the diagram, so a panel that
  // is not exactly 1240x1080 -- a different unit, or a windowed test -- gets the
  // same layout rather than one that runs off the edge.
  const float content_y = static_cast<float>(header_height_);
  const float content_h = h - content_y;
  const float vitals_h = content_h * 0.126f;
  const float context_h = content_h * 0.152f;
  const float mid_y = content_y + vitals_h;
  const float mid_h = content_h - vitals_h - context_h;
  const float left_w = w * 0.5f;
  const float sensor_h = mid_h * 0.615f;

  zones_["vitals"]     = Rect{0,       content_y, w,       vitals_h};
  zones_["sensor"]     = Rect{0,       mid_y,  left_w,     sensor_h};
  zones_["weapon"]     = Rect{0,       mid_y + sensor_h, left_w, mid_h - sensor_h};
  zones_["objectives"] = Rect{left_w,  mid_y,  w - left_w, mid_h * 0.68f};
  zones_["log"]        = Rect{left_w,  mid_y + mid_h * 0.68f, w - left_w, mid_h * 0.32f};
  zones_["context"]    = Rect{0,       h - context_h, w, context_h};

  // [zones] overrides, as "x,y,w,h" in panel pixels.
  for (auto& kv : zones_) {
    std::string key = "zones." + kv.first;
    std::string text = Cfg().GetString(key.c_str(), "");
    if (text.empty()) continue;
    float x = 0, y = 0, zw = 0, zh = 0;
    if (sscanf(text.c_str(), "%f,%f,%f,%f", &x, &y, &zw, &zh) == 4 && zw > 0 && zh > 0) {
      kv.second = Rect{x, y, zw, zh};
      DIS_INFO("[layout] zone %s overridden to %.0f,%.0f %.0fx%.0f", kv.first.c_str(),
               x, y, zw, zh);
    } else {
      DIS_WARN("[layout] zones.%s = \"%s\" is not x,y,w,h", kv.first.c_str(), text.c_str());
    }
  }

  for (const DefaultRoute& d : kDefaults) {
    Route r;
    r.destination = d.dest;
    r.zone = d.zone;
    r.interactive = d.touch;

    // An ini entry wins, and may name a zone, "main", "page" or "hidden".
    std::string override_text = Cfg().Route(d.controller, "");
    if (!override_text.empty()) {
      r.destination = ParseDest(override_text, d.dest);
      if (r.destination == Destination::kPanel) r.zone = override_text;
    }
    // A route naming a zone that does not exist would silently put the element
    // nowhere, so it is refused here and the element stays on the main screen.
    if (r.destination == Destination::kPanel && zones_.find(r.zone) == zones_.end()) {
      DIS_WARN("[layout] %s routed to unknown zone \"%s\"; keeping it on the main screen",
               d.controller, r.zone.c_str());
      r.destination = Destination::kMain;
    }
    routes_[d.controller] = r;
  }
}

const Rect* Layout::Zone(const std::string& name) const {
  auto it = zones_.find(name);
  return it == zones_.end() ? nullptr : &it->second;
}

Rect Layout::Page() const {
  return Rect{0.0f, static_cast<float>(header_height_),
              static_cast<float>(width_),
              static_cast<float>((std::max)(0, height_ - header_height_))};
}

Route Layout::For(const std::string& controller) const {
  auto it = routes_.find(controller);
  if (it != routes_.end()) return it->second;

  // Not in the table. An ini entry can still place it, which is how a class
  // this build has never heard of gets a home without a recompile.
  std::string text = Cfg().Route(controller, "");
  if (!text.empty()) {
    Route r;
    r.destination = ParseDest(text, Destination::kMain);
    if (r.destination == Destination::kPanel) {
      r.zone = text;
      if (zones_.find(r.zone) == zones_.end()) r.destination = Destination::kMain;
    }
    r.interactive = r.destination == Destination::kPage;
    return r;
  }
  return Route{};  // kMain
}

Rect Layout::FitInside(const Rect& zone, float source_w, float source_h) {
  if (source_w <= 0 || source_h <= 0 || zone.empty()) return zone;
  const float scale = (std::min)(zone.w / source_w, zone.h / source_h);
  const float w = source_w * scale;
  const float h = source_h * scale;
  return Rect{zone.x + (zone.w - w) * 0.5f, zone.y + (zone.h - h) * 0.5f, w, h};
}

void Layout::LogSummary() const {
  DIS_INFO("[layout] panel %dx%d, tab bar %dpx", width_, height_, header_height_);
  for (const auto& kv : zones_) {
    DIS_INFO("[layout]   zone %-11s %.0f,%.0f %.0fx%.0f", kv.first.c_str(),
             kv.second.x, kv.second.y, kv.second.w, kv.second.h);
  }
  int panel = 0, page = 0, main = 0, hidden = 0;
  for (const auto& kv : routes_) {
    switch (kv.second.destination) {
      case Destination::kPanel: ++panel; break;
      case Destination::kPage: ++page; break;
      case Destination::kHidden: ++hidden; break;
      default: ++main; break;
    }
  }
  DIS_INFO("[layout] routes: %d to zones, %d as pages, %d left on main, %d hidden",
           panel, page, main, hidden);
  (void)DestName;
}

}  // namespace dis
