#pragma once
#include "NativeWeaponProgress.h"
#include "UnrealBridge.h"
namespace wfcds {
// Initialize ONLY a new, still-journaled OwnerAnimator clone. Never rebind an
// existing character's animator or run TnWeapon.UpdateData. Caller retains the
// clone journal and must finish it in this same quiescent game-thread pulse.
// kStateMismatch may follow native writes; it is never a clean rejection.
NativePlayerServiceResult NativeBindClonedWeaponAnimator(HMODULE image,DWORD thread,
    void* pawn,void* weapon,UnrealObjectCloneProperty* clone);
// Read-only admission for a missing animator; validates the authored template,
// native bindings and current ownership without allocating or dispatching.
bool NativeCanPrepareWeaponAnimator(HMODULE image,DWORD thread,void* pawn,
    void* weapon,void* target_data);
// Same-pulse lazy character-service preparation, BEFORE controller detachment.
// Success commits the service to its weapon (native InitializeOwnerAnimator
// also retains an existing instance across Data changes). A failed clone/bind
// is undone immediately; dirty recovery leaves the caller's fixed-address
// journal pending and returns kStateMismatch. Never discard that journal.
NativePlayerServiceResult NativePrepareWeaponAnimator(HMODULE image,DWORD thread,
    void* pawn,void* weapon,void* target_data,UnrealObjectCloneProperty* recovery);
}
