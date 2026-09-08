#pragma once
#include <cstdint>

namespace fakeinput {
constexpr bool Xbox360Button(uint16_t code) {
    // xpad uses analog axes for LT/RT. Advertising extra digital trigger
    // buttons shifts every later button ordinal in VID/PID-specific mappings.
    return code == 0x130 || code == 0x131 || code == 0x133 || code == 0x134 ||
        code == 0x136 || code == 0x137 || (code >= 0x13a && code <= 0x13e);
}
// Translate the launcher's legacy generic evdev layout to Linux xpad's ABI.
// Merely advertising 045e:028e selects SDL's xpad mapping; it does not remap data.
constexpr uint16_t Xbox360Code(uint16_t type, uint16_t code) {
    // xpad uses BTN_X=0x133 and BTN_Y=0x134, despite their NORTH/WEST aliases.
    // The launcher's face buttons already match that ABI; keep them unchanged.
    if (type == 3) { // EV_ABS
        if (code == 0x0a) return 0x02; // left trigger: ABS_BRAKE -> ABS_Z
        if (code == 0x09) return 0x05; // right trigger: ABS_GAS -> ABS_RZ
    }
    return code;
}
}
