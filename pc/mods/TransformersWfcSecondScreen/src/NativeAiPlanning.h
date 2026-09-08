#pragma once
#include <windows.h>
#include <cstdint>

namespace wfcds {
enum class NativeAiPlanningResult { kRejected, kApplied, kStateMismatch };
bool PrepareNativeAiPlanning(HMODULE executable, DWORD game_thread, void* controller);
// These call the controller's real compiled methods, including its authority
// guard and the plan runner's CURRENT virtual Suspend/Resume implementations.
// Suspend requests native asynchronous stopping; it is not instant quiescence.
// kStateMismatch means the native call DID execute: retain rollback ownership.
NativeAiPlanningResult NativeAiSuspendPlanRunner(void* controller);
NativeAiPlanningResult NativeAiResumePlanRunner(void* controller);
// Handoff-specific lifecycle. ClearSolution must run BEFORE suspension: its
// Suspended override is a no-op. Begin cancels the pending plan, then requests
// native stopping. Drain only runs after confirmed suspension with no pending
// plan; it uses the native queue cleanup without resuming AI or starting work.
bool PrepareNativeAiPlanLifecycle(HMODULE executable, DWORD game_thread, void* controller);
NativeAiPlanningResult NativeAiBeginPlanHandoff(void* controller);
NativeAiPlanningResult NativeAiDrainStoppedPlan(void* controller);
// Read-only readiness: actual Suspended state, no pending stop, no current or
// queued solution/action/goal replacements. Also rejects nested plan mutation.
bool NativeAiPlanHandoffReady(void* controller);
// Private immediate Unbind/Bind setters, NOT public deferred Replace* calls.
// Require a drained, suspended runner and a reciprocal physical TnAiPawn pair.
// Same-set assignment runs the lifecycle without an unrooted null interval.
// Canonical identities and the opposite set must survive all native callbacks.
NativeAiPlanningResult NativeAiSetActionSet(void* controller, void* action_set);
NativeAiPlanningResult NativeAiSetGoalSet(void* controller, void* goal_set);
// Uses the compiled controller -> active world's scheduler -> SubmitRequest
// chain. Native deduplication/promotion and allocation remain engine-owned.
// Must resume first; success means exactly one accepted queue entry, not just
// that the void native method returned. No synchronous GOAP planning is run.
bool PrepareNativeAiReplanning(HMODULE executable, DWORD game_thread, void* controller);
NativeAiPlanningResult NativeAiRequestReplan(void* controller, uint8_t priority);
}  // namespace wfcds
