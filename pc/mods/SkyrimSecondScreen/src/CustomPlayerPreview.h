#pragma once

#include <cstdint>

#include <dxgi.h>

#include "Bridge.h"

namespace dis::player_preview {

constexpr uint32_t kMaxComponents = 40;
constexpr uint32_t kMaxPath = 260;

enum class ComponentKind : uint8_t {
  kBody,
  kHead,
  kHair,
  kEyes,
  kArmor,
  kHelmet,
  kGloves,
  kBoots,
  kAccessory,
  kRightWeapon,
  kLeftWeapon,
  kShield,
};

struct Component {
  uint32_t form_id = 0;
  uint32_t slot_mask = 0;
  uint8_t weapon_type = 0xFF;
  ComponentKind kind = ComponentKind::kAccessory;
  bool base_skin = false;
  char model_path[kMaxPath] = {};
  // Skyrim's _0/_1 body meshes are the authoritative endpoints for the NPC
  // weight slider. Empty for rigid equipment and head parts.
  char model_path_high[kMaxPath] = {};
};

// This is deliberately plain data: game-owned pointers never cross into the
// asynchronous asset worker or survive a PlayerCharacter update callback.
struct PlayerManifest {
  uint64_t world_epoch = 0;
  uint64_t revision = 0;
  uint32_t race_form_id = 0;
  uint32_t npc_form_id = 0;
  uint32_t body_tint_rgba = 0xFFFFFFFFu;
  float weight = 50.0f;
  uint8_t sex = 0;
  char skeleton_path[kMaxPath] = {};
  uint32_t component_count = 0;
  Component components[kMaxComponents] = {};
};

using LogLineFn = void(*)(const char* line);

void Initialize(uintptr_t skyrim_base, dis::SubmitCharacterPreviewFn submit,
                LogLineFn log_line);
void PublishManifest(const PlayerManifest& manifest);
void ResetWorld(uint64_t world_epoch);
bool HasPendingWork();
bool NeedsPresent(const dis::CharacterPreviewSurface& surface);
void RenderPresent(IDXGISwapChain* chain,
                   const dis::CharacterPreviewSurface& surface);
void Shutdown();

}  // namespace dis::player_preview
