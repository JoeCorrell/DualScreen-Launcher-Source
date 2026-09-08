#include "CharacterRoleAdapter.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <windows.h>

#include "Log.h"
#include "NativeWeaponShooter.h"
#include "NativeAiMovement.h"
#include "NativeAiAiming.h"
#include "NativeRolePawn.h"
#include "NativeWeaponExposure.h"
#include "NativePawnDamage.h"
#include "NativePawnTargeting.h"
#include "NativePawnDowned.h"
#include "NativeGameDeath.h"
#include "NativePawnTransform.h"
#include "NativePlayerCombat.h"
#include "NativeMeleeTargetGuard.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {

enum class HookId : size_t {
  kSaveActiveCheckpoint,
  kCount,
};

constexpr size_t kHookCount = static_cast<size_t>(HookId::kCount);
static_assert(sizeof(void*) == sizeof(LONG),
              "The WFC role adapter requires the 32-bit executor ABI");
static_assert(sizeof(float) == 4,
              "The WFC FVector fail-safe requires 32-bit floats");

struct HookState {
  UnrealFunctionHandle function = {};
  UnrealFunctionExecDetour detour = {};
};


struct PublishedRoleMapping {
  CharacterRoleMapping value = {};
  uint32_t player_controller_index = 0;
  uint32_t player_controlled_ai_pawn_index = 0;
  uint32_t mapped_ai_controller_index = 0;
  uint32_t ai_controlled_player_pawn_index = 0;
  uint32_t generation = 0;
};

std::array<HookState, kHookCount> g_hooks = {};
std::atomic<uint32_t> g_mapping_sequence{0};
std::atomic<void*> g_player_controller{nullptr};
std::atomic<void*> g_player_controlled_ai_pawn{nullptr};
std::atomic<void*> g_mapped_ai_controller{nullptr};
std::atomic<void*> g_ai_controlled_player_pawn{nullptr};
std::atomic<uint32_t> g_player_controller_index{0};
std::atomic<uint32_t> g_player_controlled_ai_pawn_index{0};
std::atomic<uint32_t> g_mapped_ai_controller_index{0};
std::atomic<uint32_t> g_ai_controlled_player_pawn_index{0};
std::atomic<uint32_t> g_mapping_generation{0};
std::atomic<uint32_t> g_next_generation{0};
std::atomic<bool> g_logical_player_death{false};
CheckpointGate g_checkpoint_gate;
uint32_t g_controller_pawn_offset = 0;
uint32_t g_pawn_controller_offset = 0;
uint32_t g_actor_location_offset = 0;
uint32_t g_current_form_offset = 0;
uint32_t g_vehicle_form_offset = 0;
uint32_t g_ai_weapon_shooter_offset = 0;
uint32_t g_ai_weapon_aimer_offset = 0;
uint32_t g_ai_enemy_offset = 0;
uint32_t g_pawn_weapon_offset = 0;
uint32_t g_pawn_inventory_offset = 0;
uint32_t g_shooter_owner_offset = 0;
uint32_t g_aimer_owner_offset = 0;
uint32_t g_aimer_controller_offset = 0;
uint32_t g_checkpoint_index_offset = 0;
uint32_t g_checkpoint_callback_offset = 0;
UnrealScriptDelegate g_checkpoint_callback;
uint32_t g_checkpoint_callback_object_index = 0;
bool g_ready = false;

struct RoleProxyState {
  uint32_t mapping_generation = 0;
  uint64_t next_update_at = 0;
  uint64_t next_movement_retry_at = 0;
  void* weapon = nullptr;
  void* enemy = nullptr;
  bool shooter_active = false;
  bool movement_started = false;
  bool vehicle_inputs_active = false;
};

RoleProxyState g_role_proxy;

static_assert(std::atomic<void*>::is_always_lock_free &&
                  std::atomic<uint32_t>::is_always_lock_free,
              "The role-adapter hot path requires lock-free atomic loads");

size_t HookIndex(HookId id) {
  return static_cast<size_t>(id);
}

bool LoadActiveMapping(PublishedRoleMapping* output) {
  if (!output) return false;
  for (;;) {
    const uint32_t before =
        g_mapping_sequence.load(std::memory_order_acquire);
    if (before & 1u) {
      YieldProcessor();
      continue;
    }
    PublishedRoleMapping current;
    current.value.player_controller =
        g_player_controller.load(std::memory_order_relaxed);
    current.value.player_controlled_ai_pawn =
        g_player_controlled_ai_pawn.load(std::memory_order_relaxed);
    current.value.mapped_ai_controller =
        g_mapped_ai_controller.load(std::memory_order_relaxed);
    current.value.ai_controlled_player_pawn =
        g_ai_controlled_player_pawn.load(std::memory_order_relaxed);
    current.player_controller_index =
        g_player_controller_index.load(std::memory_order_relaxed);
    current.player_controlled_ai_pawn_index =
        g_player_controlled_ai_pawn_index.load(std::memory_order_relaxed);
    current.mapped_ai_controller_index =
        g_mapped_ai_controller_index.load(std::memory_order_relaxed);
    current.ai_controlled_player_pawn_index =
        g_ai_controlled_player_pawn_index.load(std::memory_order_relaxed);
    current.generation =
        g_mapping_generation.load(std::memory_order_relaxed);
    const uint32_t after =
        g_mapping_sequence.load(std::memory_order_acquire);
    if (before == after && !(after & 1u)) {
      *output = current;
      return true;
    }
    YieldProcessor();
  }
}

bool LoadActiveMappingValue(CharacterRoleMapping* output) {
  if (!output) return false;
  for (;;) {
    const uint32_t before =
        g_mapping_sequence.load(std::memory_order_acquire);
    if (before & 1u) {
      YieldProcessor();
      continue;
    }
    CharacterRoleMapping current;
    current.player_controller =
        g_player_controller.load(std::memory_order_relaxed);
    current.player_controlled_ai_pawn =
        g_player_controlled_ai_pawn.load(std::memory_order_relaxed);
    current.mapped_ai_controller =
        g_mapped_ai_controller.load(std::memory_order_relaxed);
    current.ai_controlled_player_pawn =
        g_ai_controlled_player_pawn.load(std::memory_order_relaxed);
    const uint32_t after =
        g_mapping_sequence.load(std::memory_order_acquire);
    if (before == after && !(after & 1u)) {
      *output = current;
      return true;
    }
    YieldProcessor();
  }
}

bool MappingEmpty(const CharacterRoleMapping& mapping) {
  return !mapping.player_controller && !mapping.player_controlled_ai_pawn &&
         !mapping.mapped_ai_controller &&
         !mapping.ai_controlled_player_pawn;
}

void* RawObjectLink(void* object, uint32_t offset) {
  return object && offset
             ? *reinterpret_cast<void**>(static_cast<uint8_t*>(object) +
                                          offset)
             : nullptr;
}

struct ProxyPosition {
  float x;
  float y;
  float z;
};

float VehicleTurnToward(const UnrealStructValueView& pawn_rotation,
                        float dx, float dy) {
  int32_t current_yaw = 0;
  if (!UnrealReadStructIntField(pawn_rotation, L"Yaw", &current_yaw))
    return 0.0f;
  constexpr float kPi = 3.14159265358979323846f;
  const int32_t desired_yaw = static_cast<int32_t>(
      std::lround(std::atan2(dy, dx) * (32768.0f / kPi)));
  // UE3 rotators wrap at 65536. Narrowing the unsigned modular difference
  // to int16_t gives the shortest signed turn without a branch at the seam.
  const uint16_t wrapped = static_cast<uint16_t>(desired_yaw - current_yaw);
  const int16_t signed_delta = static_cast<int16_t>(wrapped);
  return std::clamp(static_cast<float>(signed_delta) / 8192.0f,
                    -1.0f, 1.0f);
}

bool SetVehicleProxyInputs(void* vehicle_form, float forward, float turn,
                           const UnrealStructValueView* view_rotation) {
  if (!vehicle_form || !UnrealObjectValid(vehicle_form)) return false;
  bool ok = true;
  const auto set_float = [&](const wchar_t* function_name,
                             float value) {
    if (!UnrealCallVoidFloat(vehicle_form, function_name, L"Value", value))
      ok = false;
  };
  if (UnrealObjectIsA(vehicle_form, L"TnCarForm")) {
    set_float(L"set_StrafeForwardBackInput", forward);
    set_float(L"set_StrafeLeftRightInput", 0.0f);
    set_float(L"set_SteeringInput", turn);
  } else if (UnrealObjectIsA(vehicle_form, L"TnTankForm")) {
    set_float(L"set_StrafeForwardBackInput", forward);
    set_float(L"set_StrafeRightLeftInput", 0.0f);
    set_float(L"set_TurnInput", turn);
  } else if (UnrealObjectIsA(vehicle_form, L"TnPlaneForm")) {
    set_float(L"set_StrafeForwardBackInput", forward);
    set_float(L"set_StrafeRightLeftInput", 0.0f);
    set_float(L"set_YawInput", turn);
    // Holding an inherited player pitch after the pawn becomes AI-controlled
    // makes aircraft climb or dive forever. The proxy owns this input edge.
    set_float(L"set_PitchInput", 0.0f);
  } else {
    return false;
  }
  if (view_rotation &&
      !UnrealCallVoidStruct(vehicle_form, L"set_ViewRotation", L"Value",
                            *view_rotation))
    ok = false;
  return ok;
}

bool StopMappedVehicleInputs(const CharacterRoleMapping& mapping) {
  void* current_form =
      RawObjectLink(mapping.ai_controlled_player_pawn, g_current_form_offset);
  void* vehicle_form =
      RawObjectLink(mapping.ai_controlled_player_pawn, g_vehicle_form_offset);
  if (!current_form || !vehicle_form || current_form != vehicle_form)
    return true;
  return SetVehicleProxyInputs(vehicle_form, 0.0f, 0.0f, nullptr);
}

bool StopMappedMovement(const CharacterRoleMapping& mapping) {
  void* current = RawObjectLink(mapping.ai_controlled_player_pawn, g_current_form_offset);
  void* vehicle = RawObjectLink(mapping.ai_controlled_player_pawn, g_vehicle_form_offset);
  if (!current) return false;
  if (current == vehicle) return StopMappedVehicleInputs(mapping);
  return NativeAiStopMovement(mapping.mapped_ai_controller,
      mapping.ai_controlled_player_pawn) == NativeAiMovementResult::kApplied;
}

bool ObjectSlotStillOwns(void* object, uint32_t one_based_index) {
  return object && one_based_index &&
         UnrealObjectAt(static_cast<int32_t>(one_based_index - 1u)) == object;
}

bool PlayerControllerPairLive(const CharacterRoleMapping& mapping,
                              const void* context) {
  if (!context || context != mapping.player_controller ||
      !ObjectSlotStillOwns(
          mapping.player_controller,
          g_player_controller_index.load(std::memory_order_relaxed)) ||
      !ObjectSlotStillOwns(
          mapping.player_controlled_ai_pawn,
          g_player_controlled_ai_pawn_index.load(std::memory_order_relaxed)) ||
      RawObjectLink(const_cast<void*>(context), g_controller_pawn_offset) !=
          mapping.player_controlled_ai_pawn)
    return false;
  return mapping.player_controlled_ai_pawn &&
         RawObjectLink(mapping.player_controlled_ai_pawn,
                       g_pawn_controller_offset) == context;
}

bool AiControllerPairLive(const CharacterRoleMapping& mapping,
                          const void* context) {
  if (!context || context != mapping.mapped_ai_controller ||
      !ObjectSlotStillOwns(
          mapping.mapped_ai_controller,
          g_mapped_ai_controller_index.load(std::memory_order_relaxed)) ||
      !ObjectSlotStillOwns(
          mapping.ai_controlled_player_pawn,
          g_ai_controlled_player_pawn_index.load(std::memory_order_relaxed)) ||
      RawObjectLink(const_cast<void*>(context), g_controller_pawn_offset) !=
          mapping.ai_controlled_player_pawn)
    return false;
  return mapping.ai_controlled_player_pawn &&
         RawObjectLink(mapping.ai_controlled_player_pawn,
                       g_pawn_controller_offset) == context;
}

bool PlayerControlledAiPawnPairLive(const CharacterRoleMapping& mapping,
                                    const void* context) {
  if (!context || context != mapping.player_controlled_ai_pawn ||
      !ObjectSlotStillOwns(
          mapping.player_controller,
          g_player_controller_index.load(std::memory_order_relaxed)) ||
      !ObjectSlotStillOwns(
          mapping.player_controlled_ai_pawn,
          g_player_controlled_ai_pawn_index.load(std::memory_order_relaxed)) ||
      RawObjectLink(const_cast<void*>(context), g_pawn_controller_offset) !=
          mapping.player_controller)
    return false;
  return mapping.player_controller &&
         RawObjectLink(mapping.player_controller, g_controller_pawn_offset) ==
             context;
}

bool AiControlledPlayerPawnPairLive(const CharacterRoleMapping& mapping,
                                    const void* context) {
  if (!context || context != mapping.ai_controlled_player_pawn ||
      !ObjectSlotStillOwns(
          mapping.mapped_ai_controller,
          g_mapped_ai_controller_index.load(std::memory_order_relaxed)) ||
      !ObjectSlotStillOwns(
          mapping.ai_controlled_player_pawn,
          g_ai_controlled_player_pawn_index.load(std::memory_order_relaxed)) ||
      RawObjectLink(const_cast<void*>(context), g_pawn_controller_offset) !=
          mapping.mapped_ai_controller)
    return false;
  return mapping.mapped_ai_controller &&
         RawObjectLink(mapping.mapped_ai_controller,
                       g_controller_pawn_offset) == context;
}

bool CaptureMapping(const CharacterRoleMapping& mapping,
                    PublishedRoleMapping* output) {
  if (!output) return false;
  *output = {};
  if (MappingEmpty(mapping)) return true;
  if (!mapping.player_controller || !mapping.player_controlled_ai_pawn ||
      !mapping.mapped_ai_controller || !mapping.ai_controlled_player_pawn ||
      mapping.player_controller == mapping.mapped_ai_controller ||
      mapping.player_controlled_ai_pawn ==
          mapping.ai_controlled_player_pawn ||
      !UnrealObjectValid(mapping.player_controller) ||
      !UnrealObjectIsA(mapping.player_controller, L"TnPlayerController") ||
      !UnrealObjectValid(mapping.player_controlled_ai_pawn) ||
      !UnrealObjectIsA(mapping.player_controlled_ai_pawn, L"TnAiPawn") ||
      !UnrealObjectValid(mapping.mapped_ai_controller) ||
      !UnrealObjectIsA(mapping.mapped_ai_controller, L"TnAiController") ||
      !UnrealObjectValid(mapping.ai_controlled_player_pawn) ||
      !UnrealObjectIsA(mapping.ai_controlled_player_pawn, L"TnPlayerPawn"))
    return false;

  output->value = mapping;
  output->player_controller_index =
      UnrealObjectIndex(mapping.player_controller);
  output->player_controlled_ai_pawn_index =
      UnrealObjectIndex(mapping.player_controlled_ai_pawn);
  output->mapped_ai_controller_index =
      UnrealObjectIndex(mapping.mapped_ai_controller);
  output->ai_controlled_player_pawn_index =
      UnrealObjectIndex(mapping.ai_controlled_player_pawn);
  return output->player_controller_index &&
         output->player_controlled_ai_pawn_index &&
         output->mapped_ai_controller_index &&
         output->ai_controlled_player_pawn_index;
}

bool PublishedMappingValid(const PublishedRoleMapping& mapping) {
  if (MappingEmpty(mapping.value)) return true;
  return mapping.player_controller_index &&
         mapping.player_controlled_ai_pawn_index &&
         mapping.mapped_ai_controller_index &&
         mapping.ai_controlled_player_pawn_index &&
         UnrealObjectValid(mapping.value.player_controller) &&
         UnrealObjectIndex(mapping.value.player_controller) ==
             mapping.player_controller_index &&
         UnrealObjectIsA(mapping.value.player_controller,
                         L"TnPlayerController") &&
         UnrealObjectValid(mapping.value.player_controlled_ai_pawn) &&
         UnrealObjectIndex(mapping.value.player_controlled_ai_pawn) ==
             mapping.player_controlled_ai_pawn_index &&
         UnrealObjectIsA(mapping.value.player_controlled_ai_pawn,
                         L"TnAiPawn") &&
         UnrealObjectValid(mapping.value.mapped_ai_controller) &&
         UnrealObjectIndex(mapping.value.mapped_ai_controller) ==
             mapping.mapped_ai_controller_index &&
         UnrealObjectIsA(mapping.value.mapped_ai_controller,
                         L"TnAiController") &&
         UnrealObjectValid(mapping.value.ai_controlled_player_pawn) &&
         UnrealObjectIndex(mapping.value.ai_controlled_player_pawn) ==
             mapping.ai_controlled_player_pawn_index &&
         UnrealObjectIsA(mapping.value.ai_controlled_player_pawn,
                         L"TnPlayerPawn");
}

bool PublishedObjectValid(void* object, uint32_t expected_index) {
  // The GObjects slot check rejects destruction/recycling without first
  // dereferencing a possibly stale UObject. Full class checks remain in
  // PublishedMappingValid for transaction/preflight validation.
  return ObjectSlotStillOwns(object, expected_index);
}

bool PublishedPlayerPairLive(const PublishedRoleMapping& mapping) {
  return PublishedObjectValid(mapping.value.player_controller,
                              mapping.player_controller_index) &&
         PublishedObjectValid(mapping.value.player_controlled_ai_pawn,
                              mapping.player_controlled_ai_pawn_index) &&
         PlayerControllerPairLive(mapping.value,
                                  mapping.value.player_controller);
}

bool PublishedAiPairLive(const PublishedRoleMapping& mapping) {
  return PublishedObjectValid(mapping.value.mapped_ai_controller,
                              mapping.mapped_ai_controller_index) &&
         PublishedObjectValid(mapping.value.ai_controlled_player_pawn,
                              mapping.ai_controlled_player_pawn_index) &&
         AiControllerPairLive(mapping.value,
                              mapping.value.mapped_ai_controller);
}

uint32_t NextGeneration() {
  uint32_t value =
      g_next_generation.fetch_add(1, std::memory_order_relaxed) + 1u;
  if (value == 0)
    value = g_next_generation.fetch_add(1, std::memory_order_relaxed) + 1u;
  return value;
}

PublishedRoleMapping PublishMapping(PublishedRoleMapping mapping) {
  mapping.generation = NextGeneration();
  g_mapping_sequence.fetch_add(1, std::memory_order_acq_rel);
  g_player_controller.store(mapping.value.player_controller,
                            std::memory_order_relaxed);
  g_player_controlled_ai_pawn.store(
      mapping.value.player_controlled_ai_pawn, std::memory_order_relaxed);
  g_mapped_ai_controller.store(mapping.value.mapped_ai_controller,
                               std::memory_order_relaxed);
  g_ai_controlled_player_pawn.store(
      mapping.value.ai_controlled_player_pawn, std::memory_order_relaxed);
  g_player_controller_index.store(mapping.player_controller_index,
                                  std::memory_order_relaxed);
  g_player_controlled_ai_pawn_index.store(
      mapping.player_controlled_ai_pawn_index, std::memory_order_relaxed);
  g_mapped_ai_controller_index.store(mapping.mapped_ai_controller_index,
                                     std::memory_order_relaxed);
  g_ai_controlled_player_pawn_index.store(
      mapping.ai_controlled_player_pawn_index, std::memory_order_relaxed);
  g_mapping_generation.store(mapping.generation, std::memory_order_relaxed);
  g_mapping_sequence.fetch_add(1, std::memory_order_release);
  PublishNativeRolePawnControllers(mapping.value.player_controller,
                                  mapping.value.mapped_ai_controller);
  PublishNativeWeaponExposurePawns(mapping.value.player_controlled_ai_pawn,
                                   mapping.value.ai_controlled_player_pawn);
  PublishNativePawnDamagePlayer(mapping.value.player_controlled_ai_pawn);
  PublishNativePawnTargetingPlayer(mapping.value.player_controlled_ai_pawn);
  PublishNativePawnDownedPawns(mapping.value.player_controlled_ai_pawn,
                              mapping.value.ai_controlled_player_pawn);
  PublishNativeGameDeathPawns(mapping.value.player_controlled_ai_pawn,
                             mapping.value.ai_controlled_player_pawn);
  PublishNativePlayerCombatPawns(mapping.value.player_controlled_ai_pawn,
                                mapping.value.ai_controlled_player_pawn);
  PublishNativePawnTransformGuard(mapping.value.ai_controlled_player_pawn);
  return mapping;
}

void ClearPublishedMappingWithoutToken() {
  PublishedRoleMapping empty;
  PublishMapping(empty);
}

void CallOriginal(HookId id, void* context, void* unused_edx, void* frame,
                  void* result) {
  UnrealFunctionExec original =
      g_hooks[HookIndex(id)].detour.original_executor;
  if (original) original(context, unused_edx, frame, result);
}

bool FrameIntParameter(void* frame, uint32_t offset, int32_t* output) {
  if (!frame || !output) return false;
  void* locals = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(frame) + 0x10);
  if (!locals) return false;
  *output = *reinterpret_cast<int32_t*>(
      static_cast<uint8_t*>(locals) + offset);
  return true;
}

enum class CrossRole : uint8_t { kNative, kLogicalPlayer, kLogicalAi };

CrossRole CrossRoleForPawn(void* pawn, CharacterRoleMapping* output = nullptr) {
  CharacterRoleMapping mapping;
  if (!pawn || !LoadActiveMappingValue(&mapping)) return CrossRole::kNative;
  if (pawn == mapping.player_controlled_ai_pawn &&
      PlayerControlledAiPawnPairLive(mapping, pawn)) {
    if (output) *output = mapping;
    return CrossRole::kLogicalPlayer;
  }
  if (pawn == mapping.ai_controlled_player_pawn &&
      AiControlledPlayerPawnPairLive(mapping, pawn)) {
    if (output) *output = mapping;
    return CrossRole::kLogicalAi;
  }
  return CrossRole::kNative;
}

bool ResolveNativeWeaponExposure(void* pawn) {
  return CrossRoleForPawn(pawn) == CrossRole::kLogicalPlayer;
}

// TWFC.exe+0xDE6900 copies all reflected parameters into a newly constructed
// child FFrame before calling this UFunction's +0x68 executor, and performs
// reflected local/out cleanup after it returns. These are UFunction Func-slot
// hooks, not native bytecode-token handlers, so an adapted call may return
// without advancing FFrame.Code. Non-adapted calls receive the untouched frame.

void* ResolveNativeRolePawn(void* context, bool ai) {
  CharacterRoleMapping mapping;
  if (!LoadActiveMappingValue(&mapping)) return nullptr;
  if (ai) return AiControllerPairLive(mapping, context)
      ? mapping.ai_controlled_player_pawn : nullptr;
  return PlayerControllerPairLive(mapping, context)
      ? mapping.player_controlled_ai_pawn : nullptr;
}

void* ResolveNativeDownedController(void* pawn, bool ai) {
  CharacterRoleMapping mapping;
  if (!LoadActiveMappingValue(&mapping)) return nullptr;
  if (ai) return AiControlledPlayerPawnPairLive(mapping, pawn)
      ? mapping.mapped_ai_controller : nullptr;
  return PlayerControlledAiPawnPairLive(mapping, pawn)
      ? mapping.player_controller : nullptr;
}

bool ResolveNativePlayerCombat(void* pawn, bool activating_melee) {
  CharacterRoleMapping mapping;
  if (CrossRoleForPawn(pawn, &mapping) != CrossRole::kLogicalPlayer) return false;
  if (!activating_melee) return true;
  // Native Melee uses unguarded vehicle visibility and PickedTargets[3]
  // accesses. A valid controller pair alone does not prove those resources
  // survived a scripted transition. Read/validate, never construct substitutes.
  void* vehicle = RawObjectLink(pawn, g_vehicle_form_offset);
  void* mesh = nullptr;
  void* owner = nullptr;
  void* targeting = nullptr;
  void* cylinder = nullptr;
  void* victim = nullptr;
  uint32_t mesh_offset = 0, targeting_offset = 0, cylinder_offset = 0, victim_offset = 0;
  UnrealStructArrayView picked;
  if (!vehicle || !UnrealObjectIsA(vehicle, L"TnVehicleForm") ||
      !UnrealResolveObjectPropertyOffset(vehicle, L"_MeshComponent", &mesh_offset) || mesh_offset != 0x48 ||
      !UnrealReadObjectProperty(vehicle, L"_MeshComponent", &mesh) ||
      !mesh || !UnrealObjectIsA(mesh, L"SkeletalMeshComponent") ||
      !UnrealReadObjectProperty(mesh, L"Owner", &owner) || owner != pawn ||
      !UnrealResolveObjectPropertyOffset(mapping.player_controller, L"PlayerTargeting", &targeting_offset) || targeting_offset != 0x608 ||
      !UnrealReadObjectProperty(mapping.player_controller, L"PlayerTargeting", &targeting) ||
      !targeting || !UnrealObjectIsA(targeting, L"TnPlayerTargeting") ||
      !UnrealReadStructArrayProperty(targeting, L"PickedTargets", &picked) ||
      picked.count < 4 || picked.element_size != 8 || picked.data != RawObjectLink(targeting, 0x34) ||
      !UnrealResolveStructFieldOffset({picked.data, picked.element_size, picked.struct_definition},
          L"TargetActor", L"ObjectProperty", 4, &victim_offset) || victim_offset != 0 ||
      !UnrealReadStructArrayObjectField(picked, 3, L"TargetActor", &victim) ||
      (victim && !UnrealObjectIsA(victim, L"Actor")) ||
      !UnrealResolveObjectPropertyOffset(pawn, L"CylinderComponent", &cylinder_offset) || cylinder_offset != 0x3F8 ||
      !UnrealReadObjectProperty(pawn, L"CylinderComponent", &cylinder) ||
      !cylinder || !UnrealObjectIsA(cylinder, L"CylinderComponent")) return false;
  return PlayerControlledAiPawnPairLive(mapping, pawn);
}

bool ResolveNativeMeleeTarget(void* pawn, void* targeting, void* victim) {
  // Only the game-thread native hook calls this cache. It owns no game data.
  static NativeMeleeTargetGuard cache;
  CharacterRoleMapping mapping;
  if (CrossRoleForPawn(pawn, &mapping) != CrossRole::kLogicalPlayer) return false;
  const auto generation = g_mapping_generation.load(std::memory_order_acquire);
  return NativeMeleeTargetSafe(&cache, GetCurrentThreadId(), generation,
          pawn, mapping.player_controller, targeting, victim) &&
      generation == g_mapping_generation.load(std::memory_order_acquire) &&
      PlayerControlledAiPawnPairLive(mapping, pawn);
}

bool ValidateNativeDeath(void* pawn, void* controller, NativeDeathIdentity identity) {
  PublishedRoleMapping mapping;
  if (!identity.generation || !LoadActiveMapping(&mapping) ||
      mapping.generation != identity.generation) return false;
  // GameInfo.Killed executes ScoreKill/NotifyKilled before this decision.
  // Those callbacks may detach a dying pawn. Retain the captured victim, but
  // reject object recycling, a new mapping or reassignment to another actor.
  const bool player = identity.role == NativeDeathRole::kPlayer;
  if (!player && identity.role != NativeDeathRole::kAi) return false;
  if (pawn != (player ? mapping.value.player_controlled_ai_pawn : mapping.value.ai_controlled_player_pawn) ||
      controller != (player ? mapping.value.player_controller : mapping.value.mapped_ai_controller) ||
      !PublishedObjectValid(pawn, player ? mapping.player_controlled_ai_pawn_index : mapping.ai_controlled_player_pawn_index) ||
      !PublishedObjectValid(controller, player ? mapping.player_controller_index : mapping.mapped_ai_controller_index)) return false;
  void* current_controller = RawObjectLink(pawn, g_pawn_controller_offset);
  void* current_pawn = RawObjectLink(controller, g_controller_pawn_offset);
  return (!current_controller || current_controller == controller) && (!current_pawn || current_pawn == pawn);
}

NativeDeathIdentity CaptureNativeDeath(void* pawn, void* controller) {
  PublishedRoleMapping mapping;
  if (!LoadActiveMapping(&mapping) || !mapping.generation) return {};
  NativeDeathIdentity identity;
  if (pawn == mapping.value.player_controlled_ai_pawn && controller == mapping.value.player_controller)
    identity = {NativeDeathRole::kPlayer, mapping.generation};
  else if (pawn == mapping.value.ai_controlled_player_pawn && controller == mapping.value.mapped_ai_controller)
    identity = {NativeDeathRole::kAi, mapping.generation};
  // Engine.Pawn.Died supplies Controller(Owner) if Controller was detached
  // already. Accept that exact retained victim identity, never a new owner.
  return ValidateNativeDeath(pawn, controller, identity) ? identity : NativeDeathIdentity{};
}

void NotifyNativePlayerDeath() {
  g_logical_player_death.store(true, std::memory_order_release);
}

void WFCDS_UNREAL_EXEC_CALL SaveActiveCheckpointHook(
    void* context, void* unused_edx, void* frame, void* result) {
  const bool mapping_active = CharacterRoleMappingActive();
  // Ordinary campaign saves do not belong to the swap adapter. In particular,
  // an unfamiliar frame layout must not disable saving when no swap/deferred
  // save exists. Pending/replay/failed-closed gates still inspect every call.
  if (!g_checkpoint_gate.RequiresInterception(mapping_active)) {
    CallOriginal(HookId::kSaveActiveCheckpoint, context, unused_edx, frame,
                 result);
    return;
  }
  int32_t checkpoint_index = 0;
  if (!FrameIntParameter(frame, g_checkpoint_index_offset,
                         &checkpoint_index)) {
    g_checkpoint_gate.FailClosed();
    Log(LogLevel::kError,
        "[Checkpoint] SaveActiveCheckpoint parameter capture failed; save suppressed");
    return;
  }

  const bool manager_valid = UnrealObjectValid(context) &&
                             UnrealObjectIsA(context, L"CheckpointManager");
  const uint32_t manager_index =
      manager_valid ? UnrealObjectIndex(context) : 0;
  const bool mapping_valid =
      mapping_active && manager_valid && CharacterRoleMappingObjectsValid();
  const CheckpointInterceptResult action = g_checkpoint_gate.Intercept(
      reinterpret_cast<uintptr_t>(context), manager_index, checkpoint_index,
      mapping_active, mapping_valid);

  switch (action) {
    case CheckpointInterceptResult::kRunOriginal:
      CallOriginal(HookId::kSaveActiveCheckpoint, context, unused_edx, frame,
                   result);
      return;
    case CheckpointInterceptResult::kRunDeferredOriginal: {
      DeferredCheckpointRequest replay_token;
      if (!g_checkpoint_gate.CaptureReplayToken(&replay_token)) {
        g_checkpoint_gate.FailClosed();
        return;
      }
      CallOriginal(HookId::kSaveActiveCheckpoint, context, unused_edx, frame,
                   result);
      if (!g_checkpoint_gate.CompleteReplay(
              replay_token, true))
        Log(LogLevel::kError,
            "[Checkpoint] deferred writer returned with a stale or invalid replay token; completion not acknowledged");
      else
        Log(LogLevel::kInfo,
            "[Checkpoint] deferred writer invocation returned on the verified native player graph; disk completion remains owned by the game");
      return;
    }
    case CheckpointInterceptResult::kDeferred:
      if (!UnrealReadFrameDelegateParameter(frame, g_checkpoint_callback_offset,
                                             &g_checkpoint_callback)) {
        g_checkpoint_gate.FailClosed();
        Log(LogLevel::kError, "[Checkpoint] completion callback capture failed");
        return;
      }
      g_checkpoint_callback_object_index =
          UnrealObjectIndex(g_checkpoint_callback.object);
      Log(LogLevel::kInfo,
          "[Checkpoint] save deferred until the physical player pawn is restored index=%ld",
          static_cast<long>(checkpoint_index));
      return;
    case CheckpointInterceptResult::kSuppressed:
      LogRateLimited(
          "checkpoint-save-suppressed", 1000, LogLevel::kWarning,
          "[Checkpoint] duplicate or reentrant save suppressed while one deferred request is active");
      return;
    case CheckpointInterceptResult::kFailedClosed:
      LogRateLimited(
          "checkpoint-save-failed-closed", 1000, LogLevel::kError,
          "[Checkpoint] save suppressed because checkpoint recovery could not be proven safe");
      return;
  }
}

struct HookSpec {
  // Null resolves the process-wide exact function by owner path. This is used
  // only during one-time adapter preparation for classes without a retained
  // live instance (game/weapon/camera helpers), never on a recurring path.
  void* instance;
  const wchar_t* owner_path;
  const wchar_t* function_name;
  UnrealFunctionExec replacement;
};

bool RestoreInstalledAdapters() {
  bool restored_all = true;
  for (size_t remaining = g_hooks.size(); remaining > 0; --remaining) {
    HookState& hook = g_hooks[remaining - 1];
    if (!hook.detour.installed) continue;
    if (!UnrealRestoreFunctionExecDetour(&hook.detour)) {
      restored_all = false;
      continue;
    }
    hook.function = {};
  }
  if (!RemoveNativePlayerCombat()) restored_all = false;
  if (!RemoveNativePawnTransform()) restored_all = false;
  if (!RemoveNativeGameDeath()) restored_all = false;
  if (!RemoveNativePawnDowned()) restored_all = false;
  if (!RemoveNativePawnTargeting()) restored_all = false;
  if (!RemoveNativePawnDamage()) restored_all = false;
  if (!RemoveNativeWeaponExposure()) restored_all = false;
  if (!RemoveNativeRolePawnGetters()) restored_all = false;
  return restored_all;
}

}  // namespace

bool PrepareCharacterRoleAdapter(void* player_controller_instance,
                                 void* ai_controller_instance,
                                 void* ai_pawn_instance,
                                 bool validate_only) {
  if (g_ready) return true;
  if (NativeRolePawnGettersInstalled() || NativeWeaponExposureInstalled() || NativePawnDamageInstalled() ||
      NativePawnTargetingInstalled() || NativePawnDownedInstalled() || NativeGameDeathInstalled() || NativePawnTransformInstalled() || NativePlayerCombatInstalled()) return false;
  for (const HookState& hook : g_hooks) {
    if (hook.detour.installed) return false;
  }
  if (!UnrealBridgeReady() ||
      !UnrealObjectValid(player_controller_instance) ||
      !UnrealObjectIsA(player_controller_instance, L"TnPlayerController") ||
      !UnrealObjectValid(ai_controller_instance) ||
      !UnrealObjectIsA(ai_controller_instance, L"TnAiController") ||
      !UnrealObjectValid(ai_pawn_instance) ||
      !UnrealObjectIsA(ai_pawn_instance, L"TnAiPawn"))
    return false;

  void* original_player_pawn = nullptr;
  void* player_targeting = nullptr;
  void* fine_aim_manager = nullptr;
  void* current_weapon = nullptr;
  void* ai_weapon_shooter = nullptr;
  void* ai_weapon_aimer = nullptr;
  void* ai_inventory = nullptr;
  void* player_pawn_class = nullptr;
  void* player_base_pawn_class = nullptr;
  void* ai_base_pawn_class = nullptr;
  uint32_t player_controller_pawn_offset = 0;
  uint32_t ai_controller_pawn_offset = 0;
  uint32_t player_pawn_controller_offset = 0;
  uint32_t ai_pawn_controller_offset = 0;
  uint32_t actor_location_offset = 0;
  uint32_t player_current_form_offset = 0;
  uint32_t ai_current_form_offset = 0;
  uint32_t player_vehicle_form_offset = 0;
  uint32_t ai_vehicle_form_offset = 0;
  uint32_t weapon_instigator_offset = 0;
  uint32_t ai_weapon_shooter_offset = 0;
  uint32_t ai_weapon_aimer_offset = 0;
  uint32_t ai_enemy_offset = 0;
  uint32_t player_weapon_offset = 0;
  uint32_t ai_weapon_offset = 0;
  uint32_t player_inventory_offset = 0;
  uint32_t ai_inventory_offset = 0;
  uint32_t shooter_owner_offset = 0;
  uint32_t aimer_owner_offset = 0;
  uint32_t aimer_controller_offset = 0;
  if (!UnrealReadObjectProperty(player_controller_instance, L"Pawn",
                                &original_player_pawn) ||
      !original_player_pawn ||
      !UnrealObjectIsA(original_player_pawn, L"TnPlayerPawn") ||
      !UnrealReadObjectProperty(player_controller_instance, L"PlayerTargeting",
                                &player_targeting) ||
      !player_targeting ||
      !UnrealObjectIsA(player_targeting, L"TnPlayerTargeting") ||
      !UnrealReadObjectProperty(player_controller_instance, L"_FineAimManager",
                                 &fine_aim_manager) ||
      !fine_aim_manager ||
      !UnrealObjectIsA(fine_aim_manager, L"TnFineAimManager") ||
      !UnrealReadObjectProperty(original_player_pawn, L"Weapon",
                                 &current_weapon) ||
      !current_weapon || !UnrealObjectIsA(current_weapon, L"TnWeapon") ||
      !UnrealReadObjectProperty(ai_controller_instance, L"WeaponShooter",
                                &ai_weapon_shooter) ||
      !ai_weapon_shooter ||
      !UnrealObjectIsA(ai_weapon_shooter, L"TnAiWeaponShooter") ||
      !UnrealReadObjectProperty(ai_controller_instance, L"WeaponAimer",
                                &ai_weapon_aimer) ||
      !ai_weapon_aimer ||
      !UnrealObjectIsA(ai_weapon_aimer, L"TnAiWeaponAimer") ||
      !UnrealReadObjectProperty(ai_pawn_instance, L"InvManager",
                                &ai_inventory) ||
      !ai_inventory ||
      !UnrealObjectIsA(ai_inventory, L"TnInventoryManager") ||
      !UnrealResolveClassInHierarchy(original_player_pawn, L"TnPlayerPawn",
                                     &player_pawn_class) ||
      !UnrealResolveClassInHierarchy(original_player_pawn, L"TnPawn",
                                     &player_base_pawn_class) ||
      !UnrealResolveClassInHierarchy(ai_pawn_instance, L"TnPawn",
                                     &ai_base_pawn_class) ||
      player_base_pawn_class != ai_base_pawn_class ||
      !UnrealResolveObjectPropertyOffset(player_controller_instance, L"Pawn",
                                         &player_controller_pawn_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_controller_instance, L"Pawn",
                                         &ai_controller_pawn_offset) ||
      player_controller_pawn_offset != ai_controller_pawn_offset ||
      !UnrealResolveObjectPropertyOffset(original_player_pawn, L"Controller",
                                         &player_pawn_controller_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_pawn_instance, L"Controller",
                                         &ai_pawn_controller_offset) ||
      player_pawn_controller_offset != ai_pawn_controller_offset ||
      !UnrealResolveStructPropertyOffset(ai_pawn_instance, L"Location",
                                         sizeof(float) * 3,
                                         &actor_location_offset) ||
      !UnrealResolveObjectPropertyOffset(original_player_pawn,
                                         L"_CurrentForm",
                                         &player_current_form_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_pawn_instance, L"_CurrentForm",
                                         &ai_current_form_offset) ||
      player_current_form_offset != ai_current_form_offset ||
      !UnrealResolveObjectPropertyOffset(original_player_pawn,
                                         L"_VehicleForm",
                                         &player_vehicle_form_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_pawn_instance, L"_VehicleForm",
                                         &ai_vehicle_form_offset) ||
      player_vehicle_form_offset != ai_vehicle_form_offset ||
      !UnrealResolveObjectPropertyOffset(current_weapon, L"Instigator",
                                          &weapon_instigator_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_controller_instance,
                                         L"WeaponShooter",
                                         &ai_weapon_shooter_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_controller_instance,
                                         L"WeaponAimer",
                                         &ai_weapon_aimer_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_controller_instance, L"Enemy",
                                         &ai_enemy_offset) ||
      !UnrealResolveObjectPropertyOffset(original_player_pawn, L"Weapon",
                                         &player_weapon_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_pawn_instance, L"Weapon",
                                         &ai_weapon_offset) ||
      player_weapon_offset != ai_weapon_offset ||
      !UnrealResolveObjectPropertyOffset(original_player_pawn, L"InvManager",
                                         &player_inventory_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_pawn_instance, L"InvManager",
                                         &ai_inventory_offset) ||
      player_inventory_offset != ai_inventory_offset ||
      !UnrealResolveObjectPropertyOffset(ai_weapon_shooter, L"Owner",
                                         &shooter_owner_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_weapon_aimer, L"_Owner",
                                         &aimer_owner_offset) ||
      !UnrealResolveObjectPropertyOffset(ai_weapon_aimer, L"_OwnerController",
                                         &aimer_controller_offset) ||
      RawObjectLink(ai_controller_instance, ai_weapon_shooter_offset) !=
          ai_weapon_shooter ||
      RawObjectLink(ai_controller_instance, ai_weapon_aimer_offset) !=
          ai_weapon_aimer ||
      RawObjectLink(ai_weapon_shooter, shooter_owner_offset) !=
          ai_pawn_instance ||
      RawObjectLink(ai_weapon_aimer, aimer_owner_offset) != ai_pawn_instance ||
      RawObjectLink(ai_weapon_aimer, aimer_controller_offset) !=
          ai_controller_instance) {
    Log(LogLevel::kError,
        "[RoleAdapter] current player/AI pawn class contract validation failed");
    return false;
  }

  const std::array<HookSpec, kHookCount> specs = {{
      {nullptr, L"HM_Engine.CheckpointManager", L"SaveActiveCheckpoint",
       &SaveActiveCheckpointHook},
  }};
  // All prior cooked casts now have individually validated native adapters.
  // Never substitute a global class/IsA override for their scoped checks.
  // RespawnHelper.Died is inlined into shipping TnGame.Killed. The mandatory
  // NativeGameDeath adapter below owns that player/AI decision and its native
  // OnDeath dispatch; a reflected Died executor would not intercept this path.

  const HMODULE native_image = GetModuleHandleW(nullptr);
  bool metadata_valid = ValidateNativeRolePawnCode(native_image) &&
      ValidateNativeWeaponExposureCode(native_image) && ValidateNativePawnDamage(native_image) &&
      ValidateNativePawnTargeting(native_image) && ValidateNativePawnDowned(native_image) && ValidateNativeGameDeath(native_image) &&
      ValidateNativePlayerCombat(native_image) && ValidateNativePawnTransform(native_image) &&
      player_controller_pawn_offset == 0x1C8 && player_pawn_controller_offset == 0x1D4 &&
      player_vehicle_form_offset == 0x580 &&
      NativePawnLocationDispatchSupported(native_image, original_player_pawn) &&
      NativePawnLocationDispatchSupported(native_image, ai_pawn_instance) &&
      NativePawnTransformDispatchSupported(native_image, original_player_pawn) &&
      NativePawnTransformDispatchSupported(native_image, ai_pawn_instance) &&
      actor_location_offset == 0xD8 &&
      weapon_instigator_offset == 0x94;
  if (!metadata_valid)
    Log(LogLevel::kError, "[RoleAdapter] native controller pawn/buddy role image mismatch");
  for (size_t index = 0; index < specs.size(); ++index) {
    const HookSpec& spec = specs[index];
    if ((spec.instance && UnrealResolveExactScriptFunctionForObject(
                              spec.instance, spec.owner_path,
                              spec.function_name,
                              &g_hooks[index].function)) ||
        (!spec.instance && UnrealResolveExactScriptFunction(
                               0, spec.owner_path, spec.function_name,
                               &g_hooks[index].function)))
      continue;
    Log(LogLevel::kError,
        "[RoleAdapter] exact function resolution failed at hook %lu (%ls.%ls); native port required",
        static_cast<unsigned long>(index), spec.owner_path, spec.function_name);
    metadata_valid = false;
  }

  if (!metadata_valid) {
    g_hooks.fill({});
    return false;
  }

  if (!UnrealResolveExactIntParameterOffset(
          g_hooks[HookIndex(HookId::kSaveActiveCheckpoint)].function,
          L"CheckpointIndex", &g_checkpoint_index_offset) ||
      !UnrealResolveExactDelegateParameterOffset(
          g_hooks[HookIndex(HookId::kSaveActiveCheckpoint)].function,
          L"WriteDataCompleteCallback", &g_checkpoint_callback_offset) ||
      !UnrealValidateExactIntDelegateCall(
          g_hooks[HookIndex(HookId::kSaveActiveCheckpoint)].function,
          L"CheckpointIndex", L"WriteDataCompleteCallback")) {
    Log(LogLevel::kError,
        "[RoleAdapter] exact role-event parameter layout validation failed");
    g_hooks.fill({});
    return false;
  }

  // Preflight discovers every hook/call layout without patching an executor
  // or publishing a role map. Do not wait until after AI/input quiescence to
  // discover an unrelated missing role hook.
  if (validate_only) return true;

  // Publish immutable hot-path offsets before installing any executor. The
  // mapping remains empty until the atomic transaction commit.
  g_controller_pawn_offset = player_controller_pawn_offset;
  g_pawn_controller_offset = player_pawn_controller_offset;
  g_actor_location_offset = actor_location_offset;
  g_current_form_offset = player_current_form_offset;
  g_vehicle_form_offset = player_vehicle_form_offset;
  g_ai_weapon_shooter_offset = ai_weapon_shooter_offset;
  g_ai_weapon_aimer_offset = ai_weapon_aimer_offset;
  g_ai_enemy_offset = ai_enemy_offset;
  g_pawn_weapon_offset = player_weapon_offset;
  g_pawn_inventory_offset = player_inventory_offset;
  g_shooter_owner_offset = shooter_owner_offset;
  g_aimer_owner_offset = aimer_owner_offset;
  g_aimer_controller_offset = aimer_controller_offset;

  // Do not activate the pawn/buddy hooks until every remaining role/cast/
  // lifecycle contract passed. They cannot make sibling pawn layouts safe.
  if (!InstallNativeRolePawnGetters(native_image, GetCurrentThreadId(),
                                    &ResolveNativeRolePawn)) {
    Log(LogLevel::kError, "[RoleAdapter] native pawn/buddy role installation failed");
    return false;
  }
  if (!InstallNativeWeaponExposure(native_image, GetCurrentThreadId(),
                                   &ResolveNativeWeaponExposure)) {
    Log(LogLevel::kError, "[RoleAdapter] native weapon pickup/drop/exposure installation failed");
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativePawnDamage(native_image, GetCurrentThreadId())) {
    Log(LogLevel::kError,"[RoleAdapter] native pawn damage installation failed");
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativePawnTargeting(native_image, GetCurrentThreadId(), &ResolveNativeWeaponExposure)) {
    Log(LogLevel::kError,"[RoleAdapter] native pawn targeting installation failed");
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativePawnDowned(native_image, GetCurrentThreadId(), &ResolveNativeDownedController)) {
    Log(LogLevel::kError,"[RoleAdapter] native pawn downed lifecycle installation failed");
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativeGameDeath(native_image, GetCurrentThreadId(), &CaptureNativeDeath, &ValidateNativeDeath, &NotifyNativePlayerDeath)) {
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativePawnTransform(native_image, GetCurrentThreadId())) {
    RestoreInstalledAdapters();
    return false;
  }
  if (!InstallNativePlayerCombat(native_image, GetCurrentThreadId(), &ResolveNativePlayerCombat, &ResolveNativeMeleeTarget)) {
    RestoreInstalledAdapters();
    return false;
  }
  for (size_t index = 0; index < specs.size(); ++index) {
    if (UnrealInstallFunctionExecDetour(g_hooks[index].function,
                                        specs[index].replacement,
                                        &g_hooks[index].detour))
      continue;
    Log(LogLevel::kError,
        "[RoleAdapter] executor detour installation failed at hook %lu",
        static_cast<unsigned long>(index));
    const bool rolled_back = RestoreInstalledAdapters();
    if (rolled_back) {
      g_hooks.fill({});
      g_controller_pawn_offset = 0;
      g_pawn_controller_offset = 0;
      g_actor_location_offset = 0;
      g_current_form_offset = 0;
      g_vehicle_form_offset = 0;
      g_ai_weapon_shooter_offset = 0;
      g_ai_weapon_aimer_offset = 0;
      g_ai_enemy_offset = 0;
      g_pawn_weapon_offset = 0;
      g_pawn_inventory_offset = 0;
      g_shooter_owner_offset = 0;
      g_aimer_owner_offset = 0;
      g_aimer_controller_offset = 0;
      g_checkpoint_index_offset = 0;
    }
    return false;
  }


  PublishMapping({});
  g_checkpoint_gate.Reset();
  g_ready = true;
  return true;
}

bool ShutdownCharacterRoleAdapter() {
  g_ready = false;
  ResetCharacterRoleProxy();
  g_logical_player_death.store(false, std::memory_order_release);
  g_checkpoint_gate.Reset();
  PublishMapping({});
  const bool restored_all = RestoreInstalledAdapters();
  if (restored_all) {
    g_hooks.fill({});
    g_controller_pawn_offset = 0;
    g_pawn_controller_offset = 0;
    g_actor_location_offset = 0;
    g_current_form_offset = 0;
    g_vehicle_form_offset = 0;
    g_ai_weapon_shooter_offset = 0;
    g_ai_weapon_aimer_offset = 0;
    g_ai_enemy_offset = 0;
    g_pawn_weapon_offset = 0;
    g_pawn_inventory_offset = 0;
    g_shooter_owner_offset = 0;
    g_aimer_owner_offset = 0;
    g_aimer_controller_offset = 0;
    g_checkpoint_index_offset = 0;
  }
  return restored_all;
}

bool CharacterRoleAdapterReady() {
  return g_ready;
}

bool SetCharacterRoleMapping(
    const CharacterRoleMapping& mapping,
    CharacterRoleMappingRestorePoint* restore_point) {
  if (!g_ready || !restore_point || restore_point->valid) return false;
  PublishedRoleMapping captured;
  if (!CaptureMapping(mapping, &captured)) return false;
  PublishedRoleMapping previous;
  if (!LoadActiveMapping(&previous)) return false;
  const PublishedRoleMapping published = PublishMapping(captured);
  restore_point->previous = previous.value;
  restore_point->applied_generation = published.generation;
  restore_point->valid = true;
  return true;
}

bool ClearCharacterRoleMapping(
    CharacterRoleMappingRestorePoint* restore_point) {
  return SetCharacterRoleMapping({}, restore_point);
}

bool RestoreCharacterRoleMapping(
    CharacterRoleMappingRestorePoint* restore_point) {
  if (!g_ready || !restore_point || !restore_point->valid) return false;
  PublishedRoleMapping current;
  if (!LoadActiveMapping(&current) ||
      current.generation != restore_point->applied_generation) {
    *restore_point = {};
    return false;
  }

  PublishedRoleMapping previous;
  if (!CaptureMapping(restore_point->previous, &previous)) {
    ClearPublishedMappingWithoutToken();
    *restore_point = {};
    return false;
  }
  PublishMapping(previous);
  *restore_point = {};
  return true;
}

bool InvalidateCharacterRoleMapping() {
  if (!g_ready) return false;
  PublishedRoleMapping current;
  if (!LoadActiveMapping(&current)) return false;
  // Lifecycle retirement is permitted only once neither cross-role reciprocal
  // graph is still live. UObject identity checks make stale pointers safe to
  // reject without dereferencing them.
  if (PublishedPlayerPairLive(current) || PublishedAiPairLive(current))
    return false;
  PublishMapping({});
  return true;
}

bool CharacterRoleMappingActive() {
  CharacterRoleMapping mapping;
  return LoadActiveMappingValue(&mapping) && !MappingEmpty(mapping);
}

bool CharacterRoleMappingObjectsValid() {
  PublishedRoleMapping mapping;
  return LoadActiveMapping(&mapping) && !MappingEmpty(mapping.value) &&
         PublishedMappingValid(mapping);
}

bool GetCharacterRoleMapping(CharacterRoleMapping* output) {
  if (!output) return false;
  const bool loaded = LoadActiveMappingValue(output);
  return loaded && !MappingEmpty(*output);
}

bool IsMappedPlayerController(const void* object) {
  CharacterRoleMapping mapping;
  return object && LoadActiveMappingValue(&mapping) &&
         object == mapping.player_controller;
}

bool IsMappedAiController(const void* object) {
  CharacterRoleMapping mapping;
  return object && LoadActiveMappingValue(&mapping) &&
         object == mapping.mapped_ai_controller;
}

bool IsPlayerControlledAiPawn(const void* object) {
  CharacterRoleMapping mapping;
  return object && LoadActiveMappingValue(&mapping) &&
         object == mapping.player_controlled_ai_pawn;
}

bool IsAiControlledPlayerPawn(const void* object) {
  CharacterRoleMapping mapping;
  return object && LoadActiveMappingValue(&mapping) &&
         object == mapping.ai_controlled_player_pawn;
}

bool ConsumeDeferredCheckpointNativeRestore(
    DeferredCheckpointRequest* output) {
  return g_ready && g_checkpoint_gate.ConsumeNativeRestoreRequest(output);
}

bool MarkDeferredCheckpointNativeGraphVerified(
    const DeferredCheckpointRequest& request, bool native_player_graph_valid) {
  return g_ready && g_checkpoint_gate.MarkNativeGraphVerified(
                        request, CharacterRoleMappingActive(),
                        native_player_graph_valid);
}

bool BeginDeferredCheckpointReplay(DeferredCheckpointRequest* output) {
  return g_ready && g_checkpoint_gate.BeginReplay(output);
}

bool ReplayDeferredCheckpoint() {
  DeferredCheckpointRequest request;
  if (!g_ready || !g_checkpoint_gate.BeginReplay(&request)) return false;
  void* manager = reinterpret_cast<void*>(request.manager);
  if (!UnrealObjectValid(manager) ||
      UnrealObjectIndex(manager) != request.manager_object_index ||
      !UnrealObjectIsA(manager, L"CheckpointManager") ||
      (g_checkpoint_callback.object &&
       (!UnrealObjectValid(g_checkpoint_callback.object) ||
        UnrealObjectIndex(g_checkpoint_callback.object) !=
            g_checkpoint_callback_object_index)) ||
      !UnrealCallExactVoidIntDelegate(
          manager,
          g_hooks[HookIndex(HookId::kSaveActiveCheckpoint)].function,
          L"CheckpointIndex", request.checkpoint_index,
          L"WriteDataCompleteCallback", g_checkpoint_callback) ||
      !g_checkpoint_gate.ReplayCompleted(request)) {
    g_checkpoint_gate.AbortReplay(request);
    return false;
  }
  g_checkpoint_callback = {};
  g_checkpoint_callback_object_index = 0;
  return true;
}

void AbortDeferredCheckpointReplay(const DeferredCheckpointRequest& request) {
  g_checkpoint_gate.AbortReplay(request);
}

void FailDeferredCheckpointClosed() {
  g_checkpoint_gate.FailClosed();
}

bool DeferredCheckpointPending() {
  return g_ready && g_checkpoint_gate.pending();
}

bool DeferredCheckpointFailedClosed() {
  return g_ready && g_checkpoint_gate.failed_closed();
}

void ResetCharacterRoleProxy() {
  g_role_proxy = {};
}

bool QuiesceCharacterRoleProxy() {
  if (!g_ready) {
    g_role_proxy = {};
    return true;
  }
  PublishedRoleMapping published;
  if (!LoadActiveMapping(&published) || MappingEmpty(published.value)) {
    g_role_proxy = {};
    return true;
  }
  UnrealBeginGameThreadPulse();
  bool ok = PublishedPlayerPairLive(published) &&
            PublishedAiPairLive(published);
  if (ok) {
    void* shooter = RawObjectLink(published.value.mapped_ai_controller,
                                  g_ai_weapon_shooter_offset);
    if (shooter && UnrealObjectValid(shooter)) {
      bool firing = false;
      if (!NativeWeaponShooterIsFiring(shooter, &firing)) {
        ok = false;
      } else if (firing && !NativeWeaponShooterStopFire(shooter)) {
        ok = false;
      }
    }
    if (g_role_proxy.movement_started) {
      ok = (NativeAiCancelMove(published.value.mapped_ai_controller) ==
            NativeAiMovementResult::kApplied) && ok;
      ok = StopMappedMovement(published.value) && ok;
    }
    if (g_role_proxy.vehicle_inputs_active)
      ok = StopMappedVehicleInputs(published.value) && ok;
  }
  g_role_proxy = {};
  return ok;
}

bool ConsumeLogicalPlayerDeathNotification() {
  if (!g_logical_player_death.load(std::memory_order_acquire)) return false;
  return g_logical_player_death.exchange(false, std::memory_order_acq_rel);
}

bool TickCharacterRoleProxy(uint64_t now_millis, bool* did_work) {
  if (did_work) *did_work = false;
  if (!g_ready) return true;
  const uint32_t published_generation =
      g_mapping_generation.load(std::memory_order_acquire);
  if (g_role_proxy.mapping_generation == published_generation &&
      now_millis < g_role_proxy.next_update_at)
    return true;
  PublishedRoleMapping published;
  if (!LoadActiveMapping(&published) || MappingEmpty(published.value)) {
    g_role_proxy = {};
    g_role_proxy.mapping_generation = published.generation;
    g_role_proxy.next_update_at = now_millis + 250;
    return true;
  }
  if (did_work) *did_work = true;
  g_role_proxy.next_update_at = now_millis + 250;
  if (g_role_proxy.mapping_generation != published.generation) {
    g_role_proxy = {};
    g_role_proxy.mapping_generation = published.generation;
    g_role_proxy.next_update_at = now_millis + 250;
  }
  // Each proxy slice crosses reflected memory on the game thread. Advance the
  // bounded region-retirement window before touching the mapped actor graph.
  UnrealBeginGameThreadPulse();
  if (!PublishedPlayerPairLive(published) ||
      !PublishedAiPairLive(published))
    return false;

  CharacterRoleMapping& mapping = published.value;
  bool downed = false;
  float health = 0.0f;
  if (!UnrealReadBoolProperty(mapping.ai_controlled_player_pawn,
                              L"_IsDowned", &downed) ||
      !UnrealReadFloatProperty(mapping.ai_controlled_player_pawn, L"Health",
                               &health))
    return false;
  void* current_form =
      RawObjectLink(mapping.ai_controlled_player_pawn, g_current_form_offset);
  void* vehicle_form =
      RawObjectLink(mapping.ai_controlled_player_pawn, g_vehicle_form_offset);
  if (!current_form || !vehicle_form) return false;
  const bool in_vehicle_form = current_form == vehicle_form;

  if (downed || health <= 0.0f) {
    if (g_role_proxy.shooter_active) {
      void* shooter = RawObjectLink(mapping.mapped_ai_controller,
                                    g_ai_weapon_shooter_offset);
      if (!shooter || !UnrealObjectValid(shooter) ||
          RawObjectLink(shooter, g_shooter_owner_offset) !=
              mapping.ai_controlled_player_pawn ||
          !NativeWeaponShooterStopFire(shooter))
        return false;
    }
    if (!in_vehicle_form &&
        (NativeAiCancelMove(mapping.mapped_ai_controller) != NativeAiMovementResult::kApplied ||
         !StopMappedMovement(mapping)))
      return false;
    if (in_vehicle_form && g_role_proxy.vehicle_inputs_active &&
        !SetVehicleProxyInputs(vehicle_form, 0.0f, 0.0f, nullptr))
      return false;
    g_role_proxy.shooter_active = false;
    g_role_proxy.weapon = nullptr;
    g_role_proxy.enemy = nullptr;
    g_role_proxy.movement_started = false;
    g_role_proxy.vehicle_inputs_active = false;
    return true;
  }

  // MoveTowardActor is a native Controller movement state and operates on the
  // shared Pawn/TnPawn contract. Keep its live actor destination current; the
  // function itself only pushes the state when needed.
  ProxyPosition player = {};
  ProxyPosition buddy = {};
  // PrepareCharacterRoleAdapter validates that both pawn subclasses inherit
  // the exact same Engine.Actor.Location field. Both mapped UObject slots and
  // reciprocal links were revalidated above in this game-thread pulse, so a
  // direct 12-byte snapshot is equivalent to six reflected scalar reads and
  // avoids their repeated metadata/function-validation overhead under Box64.
  std::memcpy(&player,
              static_cast<const uint8_t*>(mapping.player_controlled_ai_pawn) +
                  g_actor_location_offset,
              sizeof(player));
  std::memcpy(&buddy,
              static_cast<const uint8_t*>(mapping.ai_controlled_player_pawn) +
                  g_actor_location_offset,
              sizeof(buddy));
  const float dx = player.x - buddy.x;
  const float dy = player.y - buddy.y;
  const float dz = player.z - buddy.z;
  const float distance_squared = dx * dx + dy * dy + dz * dz;
  if (in_vehicle_form) {
    // Native vehicle AI strategies address TnAiPawn-only storage and cannot
    // safely own the physical TnPlayerPawn. Drive that pawn's existing form
    // through the same native input setters used by PlayerInCar/Tank/PlaneForm
    // instead. This preserves the actor, form, health and inventory while the
    // old player character follows the newly controlled squad member.
    if (g_role_proxy.shooter_active) {
      void* shooter = RawObjectLink(mapping.mapped_ai_controller,
                                    g_ai_weapon_shooter_offset);
      if (!shooter || !UnrealObjectValid(shooter) ||
          RawObjectLink(shooter, g_shooter_owner_offset) !=
              mapping.ai_controlled_player_pawn ||
          !NativeWeaponShooterStopFire(shooter))
        return false;
    }
    if (g_role_proxy.movement_started &&
        (NativeAiCancelMove(mapping.mapped_ai_controller) != NativeAiMovementResult::kApplied ||
         !StopMappedMovement(mapping)))
      return false;
    UnrealStructValueView pawn_rotation;
    if (!UnrealReadStructProperty(mapping.ai_controlled_player_pawn,
                                  L"Rotation", &pawn_rotation))
      return false;
    const bool should_follow = distance_squared > 500.0f * 500.0f;
    const float turn = should_follow
                           ? VehicleTurnToward(pawn_rotation, dx, dy)
                           : 0.0f;
    if (!SetVehicleProxyInputs(vehicle_form, should_follow ? 1.0f : 0.0f,
                               turn, &pawn_rotation))
      return false;
    g_role_proxy.shooter_active = false;
    g_role_proxy.weapon = nullptr;
    g_role_proxy.enemy = nullptr;
    g_role_proxy.movement_started = false;
    g_role_proxy.vehicle_inputs_active = should_follow;
    return true;
  }
  if (g_role_proxy.vehicle_inputs_active) {
    // Transformation can complete between proxy slices. Clear the prior
    // vehicle edge before robot movement takes ownership.
    if (!SetVehicleProxyInputs(vehicle_form, 0.0f, 0.0f, nullptr))
      return false;
    g_role_proxy.vehicle_inputs_active = false;
  }
  if (distance_squared > 500.0f * 500.0f) {
    // MovingToActor uses the selected pawn as its live MoveTarget, avoiding a
    // four-Hz sampled destination and the visible stop/start jitter it caused.
    // Its native guard makes repeated calls no-ops while already in state and
    // lets a later pulse re-enter after arrival or path failure.
    if (!g_role_proxy.movement_started ||
        now_millis >= g_role_proxy.next_movement_retry_at) {
      if (!UnrealCallVoidObject(mapping.mapped_ai_controller,
                                L"MoveTowardActor", L"InDestination",
                                mapping.player_controlled_ai_pawn))
        return false;
      g_role_proxy.next_movement_retry_at = now_millis + 2000;
    }
    g_role_proxy.movement_started = true;
  } else {
    if (g_role_proxy.movement_started &&
        (NativeAiCancelMove(mapping.mapped_ai_controller) != NativeAiMovementResult::kApplied ||
         !StopMappedMovement(mapping)))
      return false;
    g_role_proxy.movement_started = false;
  }

  // Most proxy slices are out of combat. Check the controller's raw Enemy
  // link before resolving services or crossing ProcessEvent for IsFiring.
  // The proxy owns every StartFire transition and caches it, so an absent
  // enemy with a quiesced cache requires no reflected cleanup at all.
  void* enemy = RawObjectLink(mapping.mapped_ai_controller, g_ai_enemy_offset);
  if ((!enemy || !UnrealObjectValid(enemy)) &&
      !g_role_proxy.shooter_active) {
    g_role_proxy.enemy = nullptr;
    return true;
  }

  // Tactical sensing remains native and TnAiSensor only uses the common
  // TnPawn contract. Resolve the aimer/shooter services only for a live enemy
  // or when a previously started firing state must be stopped.
  void* shooter = RawObjectLink(mapping.mapped_ai_controller,
                                g_ai_weapon_shooter_offset);
  void* aimer = RawObjectLink(mapping.mapped_ai_controller,
                              g_ai_weapon_aimer_offset);
  if (!shooter || !aimer || !UnrealObjectValid(shooter) ||
      !UnrealObjectValid(aimer))
    return false;
  if (RawObjectLink(shooter, g_shooter_owner_offset) !=
          mapping.ai_controlled_player_pawn ||
      RawObjectLink(aimer, g_aimer_owner_offset) !=
          mapping.ai_controlled_player_pawn ||
      RawObjectLink(aimer, g_aimer_controller_offset) !=
          mapping.mapped_ai_controller)
    return false;

  bool is_firing = false;
  if (!NativeWeaponShooterIsFiring(shooter, &is_firing)) return false;
  if (!enemy || !UnrealObjectValid(enemy)) {
    if (is_firing && !NativeWeaponShooterStopFire(shooter)) return false;
    g_role_proxy.shooter_active = false;
    // Preserve the verified active-weapon cache while temporarily out of
    // combat. Clearing it here forced Deactivate/Reset/Activate every time an
    // enemy left and re-entered sensor range, causing callback churn and
    // visible frame-time spikes.
    g_role_proxy.enemy = nullptr;
    return true;
  }
  void* pawn_weapon = RawObjectLink(mapping.ai_controlled_player_pawn,
                                    g_pawn_weapon_offset);
  void* inventory = RawObjectLink(mapping.ai_controlled_player_pawn,
                                  g_pawn_inventory_offset);
  if (!inventory || !UnrealObjectValid(inventory) ||
      !UnrealObjectIsA(inventory, L"TnInventoryManager"))
    return false;
  // TnInventoryManager exposes GetCurrentWeapon() as a function and owns no
  // CurrentWeapon field. Pawn.Weapon is the authoritative root used by the
  // game's StoreRobotWeapon/StoreVehicleWeapon implementations, so reading an
  // invented manager offset here made the role adapter impossible to prepare.
  void* weapon = pawn_weapon;
  if (!weapon) {
    if (is_firing && !NativeWeaponShooterStopFire(shooter)) return false;
    g_role_proxy.shooter_active = false;
    g_role_proxy.weapon = nullptr;
    g_role_proxy.enemy = nullptr;
    return true;
  }
  if (!UnrealObjectValid(weapon) || !UnrealObjectIsA(weapon, L"TnWeapon"))
    return false;
  void* active_weapon = nullptr;
  if (!NativeWeaponShooterActiveWeapon(shooter, &active_weapon))
    return false;
  bool shooter_changed = false;
  if (weapon != g_role_proxy.weapon || active_weapon != weapon) {
    // Possess already initializes the shooter and the transaction activates
    // its concrete weapon. Adopt that state without rebuilding callbacks.
    // Only perform the full native rebind when the equipped weapon genuinely
    // changed after a pickup or swap.
    if (active_weapon != weapon) {
      if (is_firing && !NativeWeaponShooterStopFire(shooter)) return false;
      if (!NativeWeaponShooterDeactivate(shooter) ||
          !NativeWeaponShooterReset(shooter))
        return false;
      if (!NativeWeaponShooterActivateEquipped(shooter, weapon) ||
          !NativeWeaponShooterActiveWeapon(shooter, &active_weapon) ||
          active_weapon != weapon)
        return false;
      shooter_changed = true;
    }
    g_role_proxy.weapon = weapon;
    g_role_proxy.shooter_active = false;
  }
  if (enemy != g_role_proxy.enemy && g_role_proxy.shooter_active) {
    if (!NativeWeaponShooterStopFire(shooter)) return false;
    shooter_changed = true;
    g_role_proxy.shooter_active = false;
  }
  // StopFire/rebind can change the native state within this same slice. The
  // earlier IsFiring value belongs to the previous weapon/target and must not
  // suppress StartFire or trigger another StopFire against the new state.
  if (shooter_changed && !NativeWeaponShooterIsFiring(shooter, &is_firing))
    return false;
  bool owner_is_aiming = false;
  if (!NativeUpdateAiAiming(mapping.mapped_ai_controller,
          mapping.ai_controlled_player_pawn,aimer,enemy,&owner_is_aiming))
    return false;
  if (owner_is_aiming) {
    if (!is_firing && !NativeWeaponShooterStartFire(shooter)) return false;
    g_role_proxy.shooter_active = true;
  } else {
    if (is_firing && !NativeWeaponShooterStopFire(shooter)) return false;
    g_role_proxy.shooter_active = false;
  }
  g_role_proxy.enemy = enemy;
  return true;
}

}  // namespace wfcds
