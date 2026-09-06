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
#include "UnrealBridge.h"

namespace wfcds {
namespace {

enum class HookId : size_t {
  kPlayerGetTransformerPawn,
  kAiGetTransformerPawn,
  kAiGetIsAiBuddy,
  kAiSetIsAiBuddy,
  kPlayerPawnTransform,
  kPawnStartVehicle,
  kGameKilled,
  kRespawnDied,
  kWeaponGivenTo,
  kWeaponOnPreServerFire,
  kWeaponDropFrom,
  kAiPawnHandleFlashBangDamage,
  kAiPawnOnBlockDamage,
  kAiPawnGetTargetLocation,
  kAiPawnGetAdjustedAimFor,
  kAiPawnHasSameKillTarget,
  kAiPawnClientStartDowned,
  kAiPawnClientStopDowned,
  kPlayerPawnClientStartDowned,
  kPlayerPawnClientStopDowned,
  kSaveActiveCheckpoint,
  kCount,
};

enum class CastPatchId : size_t {
  kAdjustVehicleStick,
  kIsAbleToFire,
  kAbortMelee,
  kPlayerWalkingMelee,
  kShouldRecordPlayerRecord,
  kTargetingIsOwnerJumping,
  kTargetingSetMeleeAssistTarget,
  kFineAimStart,
  kFineAimStop,
  kFineAimCheckSnipingOverheat,
  kFineAimNotifyTakeHit,
  kOnSetPawn,
  kNavigatePlayerPawn,
  kSpawnFinderPlayerPawn,
  kConstrainCameraPlayerPawn,
  kOrbitCameraPlayerPawn,
  kFlyingCameraPlayerPawn,
  kLocationCameraPlayerPawn,
  kNormalizedDownedHealthPlayerPawn,
  kLocalPlayerFinderPlayerPawn,
  kCount,
};

constexpr size_t kHookCount = static_cast<size_t>(HookId::kCount);
constexpr size_t kCastPatchCount =
    static_cast<size_t>(CastPatchId::kCount);
constexpr uint32_t kCurrentPawnCastOpcodeOffset = 6u;
constexpr uint8_t kDynamicCastOpcode = 0x2Eu;
static_assert(sizeof(void*) == sizeof(LONG),
              "The WFC role adapter requires the 32-bit executor ABI");
static_assert(sizeof(float) == 4,
              "The WFC FVector fail-safe requires 32-bit floats");

struct HookState {
  UnrealFunctionHandle function = {};
  UnrealFunctionExecDetour detour = {};
};

struct CastPatchState {
  UnrealFunctionHandle function = {};
  UnrealScriptObjectOperandPatch patch = {};
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
std::array<CastPatchState, kCastPatchCount> g_cast_patches = {};
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
uint32_t g_weapon_instigator_offset = 0;
uint32_t g_ai_weapon_shooter_offset = 0;
uint32_t g_ai_weapon_aimer_offset = 0;
uint32_t g_ai_enemy_offset = 0;
uint32_t g_pawn_weapon_offset = 0;
uint32_t g_pawn_inventory_offset = 0;
uint32_t g_shooter_owner_offset = 0;
uint32_t g_aimer_owner_offset = 0;
uint32_t g_aimer_controller_offset = 0;
UnrealFunctionHandle g_base_client_start_downed = {};
UnrealFunctionHandle g_base_client_stop_downed = {};
UnrealFunctionHandle g_player_controller_start_downed = {};
UnrealFunctionHandle g_player_controller_stop_downed = {};
UnrealFunctionHandle g_ai_controller_start_downed = {};
UnrealFunctionHandle g_ai_controller_stop_downed = {};
UnrealFunctionHandle g_player_controller_adjusted_aim = {};
UnrealPreparedObjectStructStructCall g_prepared_adjusted_aim = {};
uint32_t g_adjusted_aim_weapon_offset = 0;
uint32_t g_adjusted_aim_start_offset = 0;
uint32_t g_game_killed_player_offset = 0;
uint32_t g_game_killed_pawn_offset = 0;
uint32_t g_respawn_died_controller_offset = 0;
uint32_t g_weapon_given_to_pawn_offset = 0;
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

void* FrameObjectParameter(void* frame, uint32_t offset) {
  if (!frame) return nullptr;
  void* locals = *reinterpret_cast<void**>(
      static_cast<uint8_t*>(frame) + 0x10);
  return locals ? *reinterpret_cast<void**>(
                      static_cast<uint8_t*>(locals) + offset)
                : nullptr;
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

// TWFC.exe+0xDE6900 copies all reflected parameters into a newly constructed
// child FFrame before calling this UFunction's +0x68 executor, and performs
// reflected local/out cleanup after it returns. These are UFunction Func-slot
// hooks, not native bytecode-token handlers, so an adapted call may return
// without advancing FFrame.Code. Non-adapted calls receive the untouched frame.

void WFCDS_UNREAL_EXEC_CALL PlayerGetTransformerPawnHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context != g_player_controller.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kPlayerGetTransformerPawn, context, unused_edx,
                 frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControllerPairLive(mapping, context)) {
    if (result) {
      *static_cast<void**>(result) =
          mapping.player_controlled_ai_pawn;
    }
    return;
  }
  CallOriginal(HookId::kPlayerGetTransformerPawn, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiGetTransformerPawnHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context != g_mapped_ai_controller.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiGetTransformerPawn, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      AiControllerPairLive(mapping, context)) {
    if (result) {
      *static_cast<void**>(result) =
          mapping.ai_controlled_player_pawn;
    }
    return;
  }
  CallOriginal(HookId::kAiGetTransformerPawn, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiGetIsAiBuddyHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context != g_mapped_ai_controller.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiGetIsAiBuddy, context, unused_edx, frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      AiControllerPairLive(mapping, context)) {
    if (result) *static_cast<uint32_t*>(result) = 1u;
    return;
  }
  CallOriginal(HookId::kAiGetIsAiBuddy, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL AiSetIsAiBuddyHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context != g_mapped_ai_controller.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiSetIsAiBuddy, context, unused_edx, frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      AiControllerPairLive(mapping, context)) {
    // TnPlayerPawn has no TnAiPawn::_IsAiBuddy storage. The controller remains
    // a buddy for the lifetime of this mapping, so writes must not fall through.
    return;
  }
  CallOriginal(HookId::kAiSetIsAiBuddy, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL PlayerPawnTransformHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_ai_controlled_player_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kPlayerPawnTransform, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (!LoadActiveMappingValue(&mapping) ||
      !AiControlledPlayerPawnPairLive(mapping, context)) {
    CallOriginal(HookId::kPlayerPawnTransform, context, unused_edx, frame,
                 result);
    return;
  }
  // A TnPlayerPawn cannot safely use any native TnAi vehicle strategy because
  // those strategies directly access TnAiPawn-only avoidance storage. Keep the
  // former physical player in its verified robot form while it is driven by
  // the bounded squad proxy. This hook is transformation-only, avoiding the
  // previous class-wide TnAiController.Tick detour on every AI every frame.
  if (result) *static_cast<uint32_t*>(result) = 0u;
}

void WFCDS_UNREAL_EXEC_CALL PawnStartVehicleHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context ==
      g_ai_controlled_player_pawn.load(std::memory_order_relaxed)) {
    CharacterRoleMapping mapping;
    if (LoadActiveMappingValue(&mapping) &&
        AiControlledPlayerPawnPairLive(mapping, context))
      return;
  }
  CallOriginal(HookId::kPawnStartVehicle, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL GameKilledHook(
    void* context, void* unused_edx, void* frame, void* result) {
  void* killed_controller =
      FrameObjectParameter(frame, g_game_killed_player_offset);
  void* killed_pawn = FrameObjectParameter(frame, g_game_killed_pawn_offset);
  CharacterRoleMapping mapping;
  const bool logical_player_died =
      CrossRoleForPawn(killed_pawn, &mapping) == CrossRole::kLogicalPlayer &&
      killed_controller == mapping.player_controller;

  if (logical_player_died)
    g_logical_player_death.store(true, std::memory_order_release);

  CallOriginal(HookId::kGameKilled, context, unused_edx, frame, result);
  if (!logical_player_died || !UnrealObjectValid(context) ||
      !UnrealObjectValid(mapping.player_controller))
    return;
  void* respawn_helper = nullptr;
  if (UnrealReadObjectProperty(context, L"RespawnHelper", &respawn_helper) &&
      respawn_helper && UnrealObjectValid(respawn_helper)) {
    UnrealCallVoidObject(respawn_helper, L"Died", L"C",
                         mapping.player_controller);
  }
}

void WFCDS_UNREAL_EXEC_CALL RespawnDiedHook(
    void* context, void* unused_edx, void* frame, void* result) {
  void* dead_controller =
      FrameObjectParameter(frame, g_respawn_died_controller_offset);
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      dead_controller == mapping.mapped_ai_controller &&
      AiControllerPairLive(mapping, dead_controller)) {
    // TnGame.Killed identifies the physical TnPlayerPawn. While that actor is
    // the logical AI buddy it must not enter the player respawn pipeline.
    return;
  }
  CallOriginal(HookId::kRespawnDied, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL WeaponGivenToHook(
    void* context, void* unused_edx, void* frame, void* result) {
  void* pawn = FrameObjectParameter(frame, g_weapon_given_to_pawn_offset);
  const CrossRole role = CrossRoleForPawn(pawn);
  CallOriginal(HookId::kWeaponGivenTo, context, unused_edx, frame, result);
  if (role == CrossRole::kLogicalPlayer)
    UnrealCallVoidByte(context, L"SetWeaponDataType", L"Type", 1u);
  else if (role == CrossRole::kLogicalAi)
    UnrealCallVoidByte(context, L"SetWeaponDataType", L"Type", 2u);
}

void WFCDS_UNREAL_EXEC_CALL WeaponOnPreServerFireHook(
    void* context, void* unused_edx, void* frame, void* result) {
  void* logical_player =
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed);
  void* logical_ai =
      g_ai_controlled_player_pawn.load(std::memory_order_relaxed);
  if ((!logical_player && !logical_ai) || !g_weapon_instigator_offset) {
    CallOriginal(HookId::kWeaponOnPreServerFire, context, unused_edx, frame,
                 result);
    return;
  }
  // OnPreServerFire is global and can run at every enemy shot. Instigator is
  // an Engine.Actor field with an immutable exact-build offset, so reject all
  // unrelated weapons before entering reflection or the mapping seqlock.
  void* instigator = RawObjectLink(context, g_weapon_instigator_offset);
  if (instigator != logical_player && instigator != logical_ai) {
    CallOriginal(HookId::kWeaponOnPreServerFire, context, unused_edx, frame,
                 result);
    return;
  }
  const CrossRole role = CrossRoleForPawn(instigator);
  if (role == CrossRole::kLogicalPlayer) {
    UnrealCallVoid(instigator, L"ExposeSelf");
    return;
  }
  if (role == CrossRole::kLogicalAi) return;
  CallOriginal(HookId::kWeaponOnPreServerFire, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL WeaponDropFromHook(
    void* context, void* unused_edx, void* frame, void* result) {
  void* instigator = nullptr;
  UnrealReadObjectProperty(context, L"Instigator", &instigator);
  const CrossRole role = CrossRoleForPawn(instigator);
  int32_t clip = 0;
  int32_t reserve = 0;
  if (role == CrossRole::kLogicalPlayer) {
    UnrealReadIntProperty(context, L"AmmoClipCount", &clip);
    UnrealReadIntProperty(context, L"ReserveAmmoCount", &reserve);
  }
  CallOriginal(HookId::kWeaponDropFrom, context, unused_edx, frame, result);
  if (role == CrossRole::kNative || !result) return;
  void* dropped_pickup = *static_cast<void**>(result);
  void* dropped_weapon = nullptr;
  if (!dropped_pickup ||
      !UnrealReadObjectProperty(dropped_pickup, L"Inventory",
                                &dropped_weapon) ||
      !dropped_weapon || !UnrealObjectIsA(dropped_weapon, L"TnWeapon"))
    return;
  if (role == CrossRole::kLogicalPlayer) {
    UnrealWriteIntProperty(dropped_weapon, L"AmmoClipCount", clip);
    UnrealWriteIntProperty(dropped_weapon, L"ReserveAmmoCount", reserve);
  } else {
    UnrealCallVoid(dropped_weapon, L"SetupAmmoForDefaultPlayerPickup");
  }
}

void WFCDS_UNREAL_EXEC_CALL AiPawnHandleFlashBangDamageHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnHandleFlashBangDamage, context, unused_edx,
                 frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context))
    return;
  CallOriginal(HookId::kAiPawnHandleFlashBangDamage, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnOnBlockDamageHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnOnBlockDamage, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context))
    return;
  CallOriginal(HookId::kAiPawnOnBlockDamage, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnGetTargetLocationHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnGetTargetLocation, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context)) {
    // Engine.Actor.GetTargetLocation's base implementation returns Location.
    // Preserve that usable targeting/homing result while the AI-only
    // WeaponAimer owner is intentionally absent under the player controller.
    if (result)
      std::memcpy(result,
                  static_cast<uint8_t*>(context) + g_actor_location_offset,
                  sizeof(float) * 3);
    return;
  }
  CallOriginal(HookId::kAiPawnGetTargetLocation, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnGetAdjustedAimForHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnGetAdjustedAimFor, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context)) {
    // Exact FFrame construction at TWFC.exe+0x40D950 stores the reflected
    // locals/parameter buffer at +0x10. Forward the original W and
    // StartFireLoc to PlayerController.GetAdjustedAimFor so aim assist and the
    // live camera/controller rotation remain authoritative.
    void* locals = frame ? *reinterpret_cast<void**>(
                               static_cast<uint8_t*>(frame) + 0x10)
                         : nullptr;
    if (locals && result) {
      void* weapon = *reinterpret_cast<void**>(
          static_cast<uint8_t*>(locals) + g_adjusted_aim_weapon_offset);
      std::memset(result, 0, sizeof(int32_t) * 3);
      UnrealCallPreparedObjectStructStructResult(
          mapping.player_controller, g_prepared_adjusted_aim, weapon,
          static_cast<uint8_t*>(locals) + g_adjusted_aim_start_offset, result);
    }
    return;
  }
  CallOriginal(HookId::kAiPawnGetAdjustedAimFor, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnHasSameKillTargetHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
          g_player_controlled_ai_pawn.load(std::memory_order_relaxed) &&
      context !=
          g_ai_controlled_player_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnHasSameKillTarget, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      (PlayerControlledAiPawnPairLive(mapping, context) ||
       AiControlledPlayerPawnPairLive(mapping, context))) {
    if (result) *static_cast<uint32_t*>(result) = 0u;
    return;
  }
  CallOriginal(HookId::kAiPawnHasSameKillTarget, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnClientStartDownedHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnClientStartDowned, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context)) {
    // Match TnPlayerPawn's controller-first ordering, then execute the exact
    // common TnPawn notification/state transition once.
    UnrealCallExactVoid(mapping.player_controller,
                        g_player_controller_start_downed);
    UnrealCallExactVoid(context, g_base_client_start_downed);
    return;
  }
  CallOriginal(HookId::kAiPawnClientStartDowned, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL AiPawnClientStopDownedHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_player_controlled_ai_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kAiPawnClientStopDowned, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      PlayerControlledAiPawnPairLive(mapping, context)) {
    UnrealCallExactVoid(mapping.player_controller,
                        g_player_controller_stop_downed);
    UnrealCallExactVoid(context, g_base_client_stop_downed);
    return;
  }
  CallOriginal(HookId::kAiPawnClientStopDowned, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL PlayerPawnClientStartDownedHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_ai_controlled_player_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kPlayerPawnClientStartDowned, context, unused_edx,
                 frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      AiControlledPlayerPawnPairLive(mapping, context)) {
    // Match TnAiPawn's base-first ordering. Calling the TnPlayerPawn body here
    // would execute its hard TnPlayerController cast against an AI controller.
    UnrealCallExactVoid(context, g_base_client_start_downed);
    UnrealCallExactVoid(mapping.mapped_ai_controller,
                        g_ai_controller_start_downed);
    return;
  }
  CallOriginal(HookId::kPlayerPawnClientStartDowned, context, unused_edx,
               frame, result);
}

void WFCDS_UNREAL_EXEC_CALL PlayerPawnClientStopDownedHook(
    void* context, void* unused_edx, void* frame, void* result) {
  if (context !=
      g_ai_controlled_player_pawn.load(std::memory_order_relaxed)) {
    CallOriginal(HookId::kPlayerPawnClientStopDowned, context, unused_edx,
                 frame, result);
    return;
  }
  CharacterRoleMapping mapping;
  if (LoadActiveMappingValue(&mapping) &&
      AiControlledPlayerPawnPairLive(mapping, context)) {
    UnrealCallExactVoid(context, g_base_client_stop_downed);
    UnrealCallExactVoid(mapping.mapped_ai_controller,
                        g_ai_controller_stop_downed);
    return;
  }
  CallOriginal(HookId::kPlayerPawnClientStopDowned, context, unused_edx,
               frame, result);
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
    case CheckpointInterceptResult::kRunDeferredOriginal:
      CallOriginal(HookId::kSaveActiveCheckpoint, context, unused_edx, frame,
                   result);
      if (!g_checkpoint_gate.CompleteReplay(
              reinterpret_cast<uintptr_t>(context), manager_index, true))
        Log(LogLevel::kError,
            "[Checkpoint] deferred save returned with an invalid replay token; checkpoint gate remains closed");
      else
        Log(LogLevel::kInfo,
            "[Checkpoint] deferred save replay completed on the verified native player graph");
      return;
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

struct CastPatchSpec {
  void* instance;
  const wchar_t* owner_path;
  const wchar_t* function_name;
  uint32_t opcode_offset;
};

bool RestoreInstalledAdapters() {
  bool restored_all = true;
  for (size_t remaining = g_cast_patches.size(); remaining > 0;
       --remaining) {
    CastPatchState& cast_patch = g_cast_patches[remaining - 1];
    if (!cast_patch.patch.installed) continue;
    if (!UnrealRestoreScriptObjectOperandPatch(&cast_patch.patch)) {
      restored_all = false;
      continue;
    }
    cast_patch.function = {};
  }
  for (size_t remaining = g_hooks.size(); remaining > 0; --remaining) {
    HookState& hook = g_hooks[remaining - 1];
    if (!hook.detour.installed) continue;
    if (!UnrealRestoreFunctionExecDetour(&hook.detour)) {
      restored_all = false;
      continue;
    }
    hook.function = {};
  }
  return restored_all;
}

}  // namespace

bool PrepareCharacterRoleAdapter(void* player_controller_instance,
                                 void* ai_controller_instance,
                                 void* ai_pawn_instance,
                                 bool validate_only) {
  if (g_ready) return true;
  for (const HookState& hook : g_hooks) {
    if (hook.detour.installed) return false;
  }
  for (const CastPatchState& cast_patch : g_cast_patches) {
    if (cast_patch.patch.installed) return false;
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
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"get_TransformerPawn", &PlayerGetTransformerPawnHook},
      {ai_controller_instance, L"TransGame.TnAiController",
       L"get_TransformerPawn", &AiGetTransformerPawnHook},
      {ai_controller_instance, L"TransGame.TnAiController",
       L"get_IsAiBuddy", &AiGetIsAiBuddyHook},
      {ai_controller_instance, L"TransGame.TnAiController",
       L"set_IsAiBuddy", &AiSetIsAiBuddyHook},
      {original_player_pawn, L"TransGame.TnPlayerPawn", L"Transform",
       &PlayerPawnTransformHook},
      {original_player_pawn, L"TransGame.TnPawn", L"StartVehicle",
       &PawnStartVehicleHook},
      {nullptr, L"TransGame.TnGame", L"Killed", &GameKilledHook},
      {nullptr, L"TransGame.TnRespawnHelper", L"Died", &RespawnDiedHook},
      {nullptr, L"TransGame.TnWeapon", L"GivenTo", &WeaponGivenToHook},
      {nullptr, L"TransGame.TnWeapon", L"OnPreServerFire",
       &WeaponOnPreServerFireHook},
      {nullptr, L"TransGame.TnWeapon", L"DropFrom", &WeaponDropFromHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"HandleFlashBangDamage",
       &AiPawnHandleFlashBangDamageHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"OnBlockDamage",
       &AiPawnOnBlockDamageHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"GetTargetLocation",
       &AiPawnGetTargetLocationHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"GetAdjustedAimFor",
       &AiPawnGetAdjustedAimForHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"HasSameKillTarget",
       &AiPawnHasSameKillTargetHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"ClientStartDowned",
       &AiPawnClientStartDownedHook},
      {ai_pawn_instance, L"TransGame.TnAiPawn", L"ClientStopDowned",
       &AiPawnClientStopDownedHook},
      {original_player_pawn, L"TransGame.TnPlayerPawn",
       L"ClientStartDowned", &PlayerPawnClientStartDownedHook},
      {original_player_pawn, L"TransGame.TnPlayerPawn", L"ClientStopDowned",
       &PlayerPawnClientStopDownedHook},
      {nullptr, L"HM_Engine.CheckpointManager", L"SaveActiveCheckpoint",
       &SaveActiveCheckpointHook},
  }};
  const std::array<CastPatchSpec, kCastPatchCount> cast_specs = {{
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"AdjustStickInputForVehicleUnderMouseControl",
       kCurrentPawnCastOpcodeOffset},
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"IsAbleToFire", kCurrentPawnCastOpcodeOffset},
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"AbortMelee", kCurrentPawnCastOpcodeOffset},
      {player_controller_instance,
       L"TransGame.TnPlayerController.PlayerWalking", L"Melee",
       kCurrentPawnCastOpcodeOffset},
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"ShouldRecordPlayerRecord", kCurrentPawnCastOpcodeOffset},
      {player_targeting, L"TransGame.TnPlayerTargeting", L"IsOwnerJumping",
       0x6u},
      {player_targeting, L"TransGame.TnPlayerTargeting",
       L"SetMeleeAssistTarget", 0x4u},
      {fine_aim_manager, L"TransGame.TnFineAimManager", L"StartFineAim",
       0x26u},
      {fine_aim_manager, L"TransGame.TnFineAimManager", L"StopFineAim",
       0x28u},
      {fine_aim_manager, L"TransGame.TnFineAimManager",
       L"CheckSnipingOverheat", 0x13u},
      {fine_aim_manager, L"TransGame.TnFineAimManager", L"NotifyTakeHit",
       0x13u},
      {player_controller_instance, L"TransGame.TnPlayerController",
       L"OnSetPawn", kCurrentPawnCastOpcodeOffset},
      {nullptr, L"TransGame.TnAiActionNavigate", L"TryTeleportToLastAnchor",
       0x8Bu},
      {nullptr, L"TransGame.TnSpawnPointFinder", L"FindNearAnyPlayer",
       0x27u},
      {nullptr, L"TransGame.TnConstrainRotationByActorCameraBehavior",
       L"SetActor", 0x6u},
      {nullptr, L"TransGame.TnOrbitRotationCameraBehavior", L"SetActor",
       0x6u},
      {nullptr, L"TransGame.TnFlyingFollowCameraBehavior",
       L"AdjustStickInputForVehicleUnderMouseControl", 0x6u},
      {nullptr, L"TransGame.TnLocationOffsetCameraBehavior", L"SetActor",
       0x6u},
      {nullptr, L"TransGame.TnHudDataObserverNormalizedDownedHealth",
       L"CheckData", 0x6u},
      {nullptr, L"TransGame.TnLocalPlayerFinderOwner", L"FindLocalPlayers",
       0x6u},
  }};

  bool metadata_valid = true;
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
        "[RoleAdapter] exact function resolution failed at hook %lu",
        static_cast<unsigned long>(index));
    metadata_valid = false;
  }

  for (size_t index = 0; index < cast_specs.size(); ++index) {
    const CastPatchSpec& spec = cast_specs[index];
    if ((spec.instance && UnrealResolveExactScriptFunctionForObject(
                              spec.instance, spec.owner_path,
                              spec.function_name,
                              &g_cast_patches[index].function)) ||
        (!spec.instance && UnrealResolveExactScriptFunction(
                               0, spec.owner_path, spec.function_name,
                               &g_cast_patches[index].function)))
      continue;
    Log(LogLevel::kError,
        "[RoleAdapter] exact cast function resolution failed at patch %lu",
        static_cast<unsigned long>(index));
    metadata_valid = false;
  }
  if (!metadata_valid) {
    g_hooks.fill({});
    g_cast_patches.fill({});
    return false;
  }

  if (!UnrealResolveExactScriptFunctionForObject(
          ai_pawn_instance, L"TransGame.TnPawn", L"ClientStartDowned",
          &g_base_client_start_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          ai_pawn_instance, L"TransGame.TnPawn", L"ClientStopDowned",
          &g_base_client_stop_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          player_controller_instance, L"TransGame.TnPlayerController",
          L"StartDowned", &g_player_controller_start_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          player_controller_instance, L"TransGame.TnPlayerController",
          L"StopDowned", &g_player_controller_stop_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          ai_controller_instance, L"TransGame.TnAiController", L"StartDowned",
          &g_ai_controller_start_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          ai_controller_instance, L"TransGame.TnAiController", L"StopDowned",
          &g_ai_controller_stop_downed) ||
      !UnrealResolveExactScriptFunctionForObject(
          player_controller_instance, L"Engine.PlayerController",
          L"GetAdjustedAimFor", &g_player_controller_adjusted_aim)) {
    Log(LogLevel::kError,
        "[RoleAdapter] exact downed lifecycle resolution failed");
    g_hooks.fill({});
    g_cast_patches.fill({});
    g_base_client_start_downed = {};
    g_base_client_stop_downed = {};
    g_player_controller_start_downed = {};
    g_player_controller_stop_downed = {};
    g_ai_controller_start_downed = {};
    g_ai_controller_stop_downed = {};
    g_player_controller_adjusted_aim = {};
    g_prepared_adjusted_aim = {};
    return false;
  }


  uint32_t adjusted_aim_weapon_offset = 0;
  uint32_t adjusted_aim_start_offset = 0;
  uint32_t controller_aim_start_offset = 0;
  void* adjusted_aim_vector_definition = nullptr;
  void* controller_aim_vector_definition = nullptr;
  const UnrealFunctionHandle& ai_adjusted_aim =
      g_hooks[HookIndex(HookId::kAiPawnGetAdjustedAimFor)].function;
  if (!UnrealResolveExactObjectParameterOffset(
          ai_adjusted_aim, L"W", &adjusted_aim_weapon_offset) ||
      !UnrealResolveExactStructParameterLayout(
          ai_adjusted_aim, L"StartFireLoc", sizeof(float) * 3,
          &adjusted_aim_start_offset, &adjusted_aim_vector_definition) ||
      !UnrealResolveExactStructParameterLayout(
          g_player_controller_adjusted_aim, L"StartFireLoc",
          sizeof(float) * 3, &controller_aim_start_offset,
          &controller_aim_vector_definition) ||
      adjusted_aim_vector_definition != controller_aim_vector_definition ||
      !UnrealPrepareObjectStructStructCall(
          player_controller_instance, g_player_controller_adjusted_aim, L"W",
          L"StartFireLoc", sizeof(float) * 3, sizeof(int32_t) * 3,
          &g_prepared_adjusted_aim)) {
    Log(LogLevel::kError,
        "[RoleAdapter] exact adjusted-aim parameter layout validation failed");
    g_hooks.fill({});
    g_cast_patches.fill({});
    g_base_client_start_downed = {};
    g_base_client_stop_downed = {};
    g_player_controller_start_downed = {};
    g_player_controller_stop_downed = {};
    g_ai_controller_start_downed = {};
    g_ai_controller_stop_downed = {};
    g_player_controller_adjusted_aim = {};
    g_prepared_adjusted_aim = {};
    return false;
  }

  if (!UnrealResolveExactObjectParameterOffset(
          g_hooks[HookIndex(HookId::kGameKilled)].function,
          L"KilledPlayer", &g_game_killed_player_offset) ||
      !UnrealResolveExactObjectParameterOffset(
          g_hooks[HookIndex(HookId::kGameKilled)].function,
          L"KilledPawn", &g_game_killed_pawn_offset) ||
      !UnrealResolveExactObjectParameterOffset(
          g_hooks[HookIndex(HookId::kRespawnDied)].function, L"C",
          &g_respawn_died_controller_offset) ||
      !UnrealResolveExactObjectParameterOffset(
          g_hooks[HookIndex(HookId::kWeaponGivenTo)].function, L"ThisPawn",
          &g_weapon_given_to_pawn_offset) ||
      !UnrealResolveExactIntParameterOffset(
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
    g_cast_patches.fill({});
    g_prepared_adjusted_aim = {};
    return false;
  }

  // Preflight discovers every hook/call layout without patching an executor
  // or publishing a role map. Do not wait until after AI/input quiescence to
  // discover an unrelated missing role hook.
  bool casts_valid = true;
  for (size_t index = 0; index < cast_specs.size(); ++index) {
    if (UnrealValidateScriptObjectOperand(g_cast_patches[index].function,
          cast_specs[index].opcode_offset, kDynamicCastOpcode, player_pawn_class))
      continue;
    casts_valid = false;
    Log(LogLevel::kError,
        "[RoleAdapter] read-only cast validation failed patch=%lu owner=%ls function=%ls offset=%lu",
        static_cast<unsigned long>(index), cast_specs[index].owner_path,
        cast_specs[index].function_name,
        static_cast<unsigned long>(cast_specs[index].opcode_offset));
  }
  if (!casts_valid) return false;
  if (validate_only) return true;

  // Publish immutable hot-path offsets before installing any executor. The
  // mapping remains empty until the atomic transaction commit.
  g_controller_pawn_offset = player_controller_pawn_offset;
  g_pawn_controller_offset = player_pawn_controller_offset;
  g_actor_location_offset = actor_location_offset;
  g_current_form_offset = player_current_form_offset;
  g_vehicle_form_offset = player_vehicle_form_offset;
  g_weapon_instigator_offset = weapon_instigator_offset;
  g_ai_weapon_shooter_offset = ai_weapon_shooter_offset;
  g_ai_weapon_aimer_offset = ai_weapon_aimer_offset;
  g_ai_enemy_offset = ai_enemy_offset;
  g_pawn_weapon_offset = player_weapon_offset;
  g_pawn_inventory_offset = player_inventory_offset;
  g_shooter_owner_offset = shooter_owner_offset;
  g_aimer_owner_offset = aimer_owner_offset;
  g_aimer_controller_offset = aimer_controller_offset;
  g_adjusted_aim_weapon_offset = adjusted_aim_weapon_offset;
  g_adjusted_aim_start_offset = adjusted_aim_start_offset;

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
      g_cast_patches.fill({});
      g_controller_pawn_offset = 0;
      g_pawn_controller_offset = 0;
      g_actor_location_offset = 0;
      g_current_form_offset = 0;
      g_vehicle_form_offset = 0;
      g_weapon_instigator_offset = 0;
      g_ai_weapon_shooter_offset = 0;
      g_ai_weapon_aimer_offset = 0;
      g_ai_enemy_offset = 0;
      g_pawn_weapon_offset = 0;
      g_pawn_inventory_offset = 0;
      g_shooter_owner_offset = 0;
      g_aimer_owner_offset = 0;
      g_aimer_controller_offset = 0;
      g_adjusted_aim_weapon_offset = 0;
      g_adjusted_aim_start_offset = 0;
      g_checkpoint_index_offset = 0;
      g_prepared_adjusted_aim = {};
    }
    return false;
  }


  for (size_t index = 0; index < cast_specs.size(); ++index) {
    CastPatchState& state = g_cast_patches[index];
    if (UnrealInstallScriptObjectOperandPatch(
            state.function, cast_specs[index].opcode_offset,
            kDynamicCastOpcode, player_pawn_class, player_base_pawn_class,
            &state.patch))
      continue;
    Log(LogLevel::kError,
        "[RoleAdapter] Script cast patch installation failed at patch %lu",
        static_cast<unsigned long>(index));
    const bool rolled_back = RestoreInstalledAdapters();
    if (rolled_back) {
      g_hooks.fill({});
      g_cast_patches.fill({});
      g_controller_pawn_offset = 0;
      g_pawn_controller_offset = 0;
      g_actor_location_offset = 0;
      g_current_form_offset = 0;
      g_vehicle_form_offset = 0;
      g_weapon_instigator_offset = 0;
      g_ai_weapon_shooter_offset = 0;
      g_ai_weapon_aimer_offset = 0;
      g_ai_enemy_offset = 0;
      g_pawn_weapon_offset = 0;
      g_pawn_inventory_offset = 0;
      g_shooter_owner_offset = 0;
      g_aimer_owner_offset = 0;
      g_aimer_controller_offset = 0;
      g_adjusted_aim_weapon_offset = 0;
      g_adjusted_aim_start_offset = 0;
      g_checkpoint_index_offset = 0;
      g_prepared_adjusted_aim = {};
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
    g_cast_patches.fill({});
    g_controller_pawn_offset = 0;
    g_pawn_controller_offset = 0;
    g_actor_location_offset = 0;
    g_current_form_offset = 0;
    g_vehicle_form_offset = 0;
    g_weapon_instigator_offset = 0;
    g_ai_weapon_shooter_offset = 0;
    g_ai_weapon_aimer_offset = 0;
    g_ai_enemy_offset = 0;
    g_pawn_weapon_offset = 0;
    g_pawn_inventory_offset = 0;
    g_shooter_owner_offset = 0;
    g_aimer_owner_offset = 0;
    g_aimer_controller_offset = 0;
    g_base_client_start_downed = {};
    g_base_client_stop_downed = {};
    g_player_controller_start_downed = {};
    g_player_controller_stop_downed = {};
    g_ai_controller_start_downed = {};
    g_ai_controller_stop_downed = {};
    g_player_controller_adjusted_aim = {};
    g_prepared_adjusted_aim = {};
    g_adjusted_aim_weapon_offset = 0;
    g_adjusted_aim_start_offset = 0;
    g_game_killed_player_offset = 0;
    g_game_killed_pawn_offset = 0;
    g_respawn_died_controller_offset = 0;
    g_weapon_given_to_pawn_offset = 0;
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
    bool native_player_graph_valid) {
  return g_ready && g_checkpoint_gate.MarkNativeGraphVerified(
                        CharacterRoleMappingActive(),
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
      g_checkpoint_gate.pending() || g_checkpoint_gate.failed_closed()) {
    g_checkpoint_gate.AbortReplay();
    return false;
  }
  g_checkpoint_callback = {};
  g_checkpoint_callback_object_index = 0;
  return true;
}

void AbortDeferredCheckpointReplay() {
  g_checkpoint_gate.AbortReplay();
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
