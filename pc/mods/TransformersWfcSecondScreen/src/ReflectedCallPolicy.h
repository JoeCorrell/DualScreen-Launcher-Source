#pragma once

#include <cstdint>

namespace wfcds {

enum class ReflectedCallKind { kRejected, kScript, kEmpty, kNative };

// TWFC.exe 116e690b..116e6949: unindexed Defined/Native functions execute;
// empty declarations return without touching the caller's parameter storage.
// Keep hook eligibility separate: an empty function has no executor to detour.
constexpr ReflectedCallKind ClassifyReflectedCall(uint32_t flags,
                                                 uint16_t native_token,
                                                 bool native_allowlisted) {
  if (native_token || (flags & (0x0000000Cu | 0x18000000u)))
    return ReflectedCallKind::kRejected;
  if (flags & 0x400u)
    return native_allowlisted ? ReflectedCallKind::kNative
                              : ReflectedCallKind::kRejected;
  return flags & 2u ? ReflectedCallKind::kScript : ReflectedCallKind::kEmpty;
}

constexpr bool IntDelegateCallLayoutValid(uint32_t parameter_count,
                                         uint32_t parameter_bytes,
                                         uint32_t return_offset,
                                         uint32_t int_offset,
                                         uint32_t delegate_offset) {
  return parameter_count == 2 && return_offset == 0xFFFFu &&
         parameter_bytes >= 20 && parameter_bytes <= 1024 &&
         int_offset <= parameter_bytes - 4 &&
         delegate_offset <= parameter_bytes - 16 &&
         (int_offset + 4 <= delegate_offset || delegate_offset + 16 <= int_offset);
}

}  // namespace wfcds
