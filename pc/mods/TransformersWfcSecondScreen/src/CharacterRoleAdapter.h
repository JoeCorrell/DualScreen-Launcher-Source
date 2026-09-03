#pragma once

#include <cstdint>

#include "CheckpointGate.h"

namespace wfcds {

// The two cross-role bindings required while control is transferred. The
// player controller sees player_controlled_ai_pawn as its TransformerPawn;
// mapped_ai_controller sees ai_controlled_player_pawn as its TransformerPawn.
// Every pointer must be a live UObject of the named WFC class when published.
struct CharacterRoleMapping {
  void* player_controller = nullptr;
  void* player_controlled_ai_pawn = nullptr;
  void* mapped_ai_controller = nullptr;
  void* ai_controlled_player_pawn = nullptr;
};

// A single-use rollback point returned by Set/Clear. Restore refuses a stale
// rollback point if another mapping has since been published.
struct CharacterRoleMappingRestorePoint {
  CharacterRoleMapping previous = {};
  uint32_t applied_generation = 0;
  bool valid = false;
};

// Resolves and installs all required exact UFunction executor detours. The
// supplied live instances are used only to walk their declaring-class fields;
// no process-wide GObjects scan is performed. Call on the game thread.
bool PrepareCharacterRoleAdapter(void* player_controller_instance,
                                 void* ai_controller_instance,
                                 void* ai_pawn_instance);
// Clears the mapping first, then restores every installed executor detour in
// reverse order. Call before ShutdownUnrealBridge. Returns false if an executor
// no longer contained our hook.
bool ShutdownCharacterRoleAdapter();
bool CharacterRoleAdapterReady();

// Publishes all four bindings as one coherent lock-free snapshot after
// validating their live UObject identities and class contracts. Mapping
// publication and restoration must be serialized on the game thread.
bool SetCharacterRoleMapping(
    const CharacterRoleMapping& mapping,
    CharacterRoleMappingRestorePoint* restore_point);
bool ClearCharacterRoleMapping(
    CharacterRoleMappingRestorePoint* restore_point);
bool RestoreCharacterRoleMapping(
    CharacterRoleMappingRestorePoint* restore_point);
// Publishes an empty mapping and invalidates any outstanding restore point
// without removing the process-wide adapters. Use on game-thread world/epoch
// invalidation after the engine has dismantled the old controller graph.
bool InvalidateCharacterRoleMapping();

bool CharacterRoleMappingActive();
bool CharacterRoleMappingObjectsValid();
bool GetCharacterRoleMapping(CharacterRoleMapping* output);

// Lock-free predicates used by the swap transaction. They deliberately do not
// perform reflection; call CharacterRoleMappingObjectsValid at transaction
// boundaries to reject stale UObjects.
bool IsMappedPlayerController(const void* object);
bool IsMappedAiController(const void* object);
bool IsPlayerControlledAiPawn(const void* object);
bool IsAiControlledPlayerPawn(const void* object);

// While the physical TnPlayerPawn is assigned to an AI controller, native
// GOAP cannot be rebound safely because its actions directly address
// TnAiPawn-only storage. This bounded game-thread proxy keeps that one actor
// following the active player and using its existing weapon while the native
// planner remains suspended. It performs no work when no cross-role mapping
// is active and internally rate-limits reflected calls.
bool TickCharacterRoleProxy(uint64_t now_millis, bool* did_work);
// Stops any native movement/fire edge owned by the bounded proxy before its
// cached state is discarded. This is used on death and world invalidation so
// ResetCharacterRoleProxy cannot leave an asynchronous AI action running.
bool QuiesceCharacterRoleProxy();
bool ConsumeLogicalPlayerDeathNotification();
void ResetCharacterRoleProxy();

// SaveActiveCheckpoint cannot serialize a player-controlled TnAiPawn. The
// executor hook retains the first request while a cross-role mapping is live.
// GameBridge consumes it, performs the normal verified squad transaction back
// to the physical TnPlayerPawn, then arms one exact reflected replay.
bool ConsumeDeferredCheckpointNativeRestore(
    DeferredCheckpointRequest* output);
bool MarkDeferredCheckpointNativeGraphVerified(
    bool native_player_graph_valid);
bool BeginDeferredCheckpointReplay(DeferredCheckpointRequest* output);
// Executes the retained manager request through the exact hooked base
// UFunction. Returns true only when the hook admitted the request, the
// original executor returned, and the one-shot gate returned to idle.
bool ReplayDeferredCheckpoint();
void AbortDeferredCheckpointReplay();
void FailDeferredCheckpointClosed();
bool DeferredCheckpointPending();
bool DeferredCheckpointFailedClosed();

}  // namespace wfcds
