#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace wfcds {
struct AbilityHudSlot {
  bool present=false;
  float remaining=0;
  // Effective native GetCooldownTime result, including skill data and buffs.
  // Do not substitute a cached configured Cooldown[0/1] for this value.
  float effective_duration=0;
};
struct AbilityHudCommand {
  uint32_t slot=0;
  bool ready=false;
  float duration=0,progress=0;
};
struct AbilityHudPlan {
  std::array<AbilityHudCommand,4> commands{};
  uint32_t count=0;
};
// Read-only reconstruction of StartAbilityCooldown / UpdateResourceUI. The
// caller must capture actual native effective durations and resource cost on
// the game thread. This does not alter Cooldowns, Resource or any ability.
inline bool BuildAbilityHudPlan(const std::array<AbilityHudSlot,3>& slots,
    float multiplier,float resource,float resource_required,AbilityHudPlan* out) {
  if (!out) return false;
  *out={}; AbilityHudPlan result;
  if (!std::isfinite(multiplier) || multiplier<0 || !std::isfinite(resource) || resource<0 ||
      !std::isfinite(resource_required) || resource_required<0) return false;
  constexpr float waiting=std::numeric_limits<float>::max();
  const auto append=[&](uint32_t slot,bool ready,float duration,float progress) {
    if (!std::isfinite(duration) || !std::isfinite(progress)) return false;
    result.commands[result.count++]={slot,ready,duration,progress}; return true;
  };
  for (uint32_t i=0;i<slots.size();++i) {
    const auto& s=slots[i];
    if (!s.present) { append(i,true,-1,0); continue; }
    if (!std::isfinite(s.remaining)) return false;
    if (!i && resource_required>0) {
      if (!append(i,false,0,resource/resource_required)) return false;
      if (resource>=resource_required) append(i,true,-1,0);
    } else if (s.remaining<=0) {
      append(i,true,-1,0);
    } else {
      // FLT_MAX means the ability has not allowed cooldown to start yet.
      // A zero multiplier pauses an already-started cooldown at its progress.
      const bool pending=s.remaining==waiting;
      if (!pending && (!std::isfinite(s.effective_duration) || s.effective_duration<=0)) return false;
      const float progress=pending ? 0.0f : 1.0f-s.remaining/s.effective_duration;
      const float duration=pending || multiplier==0 ? waiting : s.remaining/multiplier;
      if (!append(i,false,duration,progress)) return false;
    }
  }
  *out=result; return true;
}
} // namespace wfcds
