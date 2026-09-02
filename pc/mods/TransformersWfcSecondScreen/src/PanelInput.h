#pragma once

#include <cstddef>
#include <cstdint>

namespace wfcds {

enum class PanelInputKind : uint8_t {
  kInvalid,
  kTap,
  kDown,
  kMove,
  kUp,
  kHold,
  kCancel,
  kPageNext,
  kPagePrevious,
  kNavLeft,
  kNavRight,
  kNavUp,
  kNavDown,
  kNavActivate,
};

struct PanelInput {
  PanelInputKind kind = PanelInputKind::kInvalid;
  float x = 0.0f;
  float y = 0.0f;
};

bool ParsePanelInput(const char* bytes, size_t size, PanelInput* output);

enum class PanelPage : uint8_t { kCombat, kSquad, kObjectives };

enum class HitTarget : uint8_t {
  kNone,
  kPreviousPage,
  kCombatPage,
  kSquadPage,
  kObjectivesPage,
  kNextPage,
  kAbility1,
  kAbility2,
  kGrenade,
  kTransform,
  kReload,
  kSwap,
  kSquad0,
  kSquad1,
  kSquad2,
};

HitTarget HitTestPanel(PanelPage page, float x, float y,
                       uint32_t squad_count);

}  // namespace wfcds
