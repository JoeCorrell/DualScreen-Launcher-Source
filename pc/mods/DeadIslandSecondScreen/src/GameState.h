// What the panel draws, and where each number came from.
//
// Every value here is read from the live player object by name through the
// engine's reflection API. Nothing is cached across frames except the *binding*
// -- which spelling of a name this build answers to -- because that is settled
// once and the value behind it changes constantly.
//
// The rule this file follows, and the reason it is written this way: a value
// that could not be read is `valid == false`, and a page draws an unavailable
// state for it. It is never zero, and never the last number that happened to
// work. A HUD that shows 0 health when it has simply lost the player is worse
// than one that says so, because it looks exactly like being about to die.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dis {

using CRTTIObject = void;

// A number that knows whether it is real, and what it was called.
struct Value {
  double v = 0.0;
  bool valid = false;
  const char* bound_to = nullptr;  // the name that answered, for the log

  float f() const { return static_cast<float>(v); }
  int i() const { return static_cast<int>(v); }
  // Fraction of `max`, clamped, or -1 when either side is unavailable. Bars
  // use this so a missing maximum draws nothing rather than a full bar.
  static float Ratio(const Value& cur, const Value& max);
};

struct Vitals {
  Value health, max_health;
  Value stamina, max_stamina;
  Value fury, max_fury;
  Value fury_active;
  Value level, experience, exp_to_next;
  Value money, skill_points;

  // True when at least the health pair bound. Below that there is no useful
  // HUD to draw and the page says so.
  bool usable() const { return health.valid; }
};

// Capacity candidates are resolved against the live PlayerDI object through
// FindField/GetField. Missing fields remain invalid; no private layout is
// assumed and no menu controller is opened or retained.
// Item rows are intentionally not represented here: this game build has no
// typed CRTTIObject getter for PlayerInventory's container, so treating its
// private layout as a vector would be an unsafe offset guess.
struct InventoryCapacity {
  Value slots, max_slots;
  Value quick_slots, max_quick_slots;

  bool usable() const { return slots.valid || quick_slots.valid; }
};

class GameState {
 public:
  // Rebinds names against the current player. Cheap to call; does nothing once
  // bound unless the player changed.
  void Bind(CRTTIObject* player);
  void Unbind(const char* reason);
  bool bound() const { return bound_; }

  // Re-reads every bound value. Call once per panel frame, not per page.
  void Sample(CRTTIObject* player);

  const Vitals& vitals() const { return vitals_; }
  const InventoryCapacity& inventory_capacity() const {
    return inventory_capacity_;
  }

  // Writes the full field sweep to the log: every name in catalog::kSweep that
  // this object actually declares, with its current value and type. This is
  // the output that turns the candidate lists into facts, and it runs once per
  // bind rather than on a timer.
  void Dump(CRTTIObject* player, const char* label) const;

#ifdef DIS_UI_PREVIEW
  void SetPreviewData();
#endif

 private:
  bool bound_ = false;
  const void* bound_player_ = nullptr;
  Vitals vitals_;
  InventoryCapacity inventory_capacity_;
  double max_health_observed_ = 0.0;
};

GameState& TheGameState();

}  // namespace dis
