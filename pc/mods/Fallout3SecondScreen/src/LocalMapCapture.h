#pragma once

#include <cstdint>

#include "Version.h"

namespace fo3ds {

enum class LocalMapPumpResult : uint8_t {
  kIdle,
  kBuilding,
  kPublished,
  kFailed,
};

enum class LocalMapCaptureStatus : uint8_t {
  kUnavailable,
  kIdle,
  kBuilding,
  kRetrying,
  kReady,
};

bool InitializeLocalMapCapture(const BuildIdentity& build,
                               uintptr_t module_base);
void ResetLocalMapCapture(uint64_t world_epoch);
LocalMapPumpResult PumpLocalMapCapture(void* player, uint64_t world_epoch,
                                       bool requested);
bool IsLocalMapFrameReady(uint64_t world_epoch, uint32_t cell_form_id);
LocalMapCaptureStatus GetLocalMapCaptureStatus();
void ShutdownLocalMapCapture();

}  // namespace fo3ds
