#pragma once
#include "UnrealBridge.h"
namespace wfcds {
// Non-owning, game-thread-only cache of an engine-owned targeting buffer.
// It never holds temporary game allocations or extends an actor's lifetime.
struct NativeMeleeTargetGuard {
  DWORD thread=0;
  uint32_t generation=0;
  void* pawn=nullptr;void* controller=nullptr;void* targeting=nullptr;
  uint32_t pawn_index=0,controller_index=0,targeting_index=0;
  UnrealArrayView targets{};
};
// Caller must validate the canonical logical-player mapping before and after.
// Unchanged buffers use GObjects slots/raw links, not repeated reflection or
// VirtualQuery. Mapping changes, recycled objects or resized arrays revalidate.
bool NativeMeleeTargetSafe(NativeMeleeTargetGuard*,DWORD,uint32_t generation,
    void* pawn,void* controller,void* targeting,void* victim);
}
