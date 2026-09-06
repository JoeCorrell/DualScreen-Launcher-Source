#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativePlayerServiceResult { kRejected, kApplied, kStateMismatch };
// Ports of the compiled service initialization leaves, NOT a replacement for
// OnSetPawn, possession, or the physical AI/player role adapter. The caller
// must pass the complete role/mission audit before changing controller links.
bool PrepareNativePlayerServices(HMODULE executable, DWORD game_thread, void* player_controller);
NativePlayerServiceResult NativeInitializePickup(void* manager, void* pawn, void* commander);
NativePlayerServiceResult NativeInitializeFineAim(void* manager, void* pawn, void* targeting, void* camera);
// HmPlayerTargeting.set_TargetingOwner is a plain reference assignment in the
// cooked source. Use the GC-aware writer, not a stripped reflected executor.
NativePlayerServiceResult NativeSetTargetingOwner(void* targeting, void* pawn);
// Exact pure BuffManager.SetHud semantics through the shared GC-aware writer.
// A non-null HUD may attach only to the current acknowledged player pawn;
// clearing also supports the former player now owned by the squad AI.
bool PrepareNativeBuffHud(void* player_controller, void* squad_pawn);
NativePlayerServiceResult NativeSetBuffHud(void* player_controller, void* pawn, void* hud);
// Actual manager-owned state, not configured/default cooldowns or HUD estimates.
// No effective buff/skill-adjusted durations or resource costs are inferred here.
struct NativeAbilityState {
  void *pawn=nullptr,*manager=nullptr,*hud=nullptr;
  uint32_t pawn_index=0,manager_index=0,count=0;
  std::array<void*,3> abilities{};
  std::array<uint32_t,3> ability_indices{};
  std::array<float,3> cooldowns{},spam_cooldowns{};
  float multiplier=0,resource=0;
};
bool NativeReadAbilityState(void* player_controller,void* pawn,NativeAbilityState* out);
// Null-only leaf: removes HUD ownership without replaying SetHud's ready
// notifications or resetting any manager/ability runtime state. The caller's
// swap journal must already own rollback before calling this mutation.
NativePlayerServiceResult NativeClearAbilityHud(void* player_controller,void* pawn);
// Read-only TnPawn -> current-form buff-service resolution. Success + null
// means the supported native path has no buff service; failure is not "no buff".
bool NativeResolveAbilityBuffService(void* player_controller,void* pawn,void** service);
// Effective GetCooldownTime values for the existing three ability slots.
// Supports verified literal native skill selectors; unknown overrides reject.
// Does not attach a HUD or change timers.
bool NativeReadAbilityCooldowns(void* player_controller,void* pawn,std::array<float,3>* output);
// Existing manager slot-zero GetResourceRequired, including the supported
// product/difficulty buff path. Unknown modifiers/conditions reject, not zero.
// No resource consumption, initialization, buff application or HUD attachment.
bool NativeReadAbilityResourceRequired(void* player_controller,void* pawn,float* output);
// Read-only admission works for either existing squad pawn. Non-null restore
// requires the acknowledged player pair, held input leases and an armed journal.
bool PrepareNativeAbilityHudRestore(void* player_controller,void* pawn,void* hud);
NativePlayerServiceResult NativeRestoreAbilityHud(void* player_controller,void* pawn,void* hud,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
}  // namespace wfcds
