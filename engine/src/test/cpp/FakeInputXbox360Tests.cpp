#include "../../main/cpp/wemu/FakeInputXbox360.h"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    constexpr std::array<unsigned short, 11> buttons{
        0x130, 0x131, 0x133, 0x134, 0x136, 0x137, 0x13a, 0x13b, 0x13c, 0x13d, 0x13e};
    unsigned ordinal = 0;
    for (unsigned short code = 0; code < 0x300; ++code) {
        if (!fakeinput::Xbox360Button(code)) continue;
        assert(ordinal < buttons.size() && buttons[ordinal++] == code);
    }
    assert(ordinal == buttons.size());
    // Legacy FakeInputWriter order -> ascending Linux xpad axes, as consumed
    // by a VID/PID-specific Xbox mapping: LX, LY, LT, RX, RY, RT, hatX, hatY.
    constexpr std::array<unsigned short, 8> source{0, 1, 10, 3, 4, 9, 16, 17};
    constexpr std::array<unsigned short, 8> expected{0, 1, 2, 3, 4, 5, 16, 17};
    for (unsigned i = 0; i < source.size(); ++i)
        assert(fakeinput::Xbox360Code(3, source[i]) == expected[i]);
    // xpad's historical BTN_X/BTN_Y aliases are NORTH/WEST. Do not swap them.
    for (unsigned short key = 0x130; key <= 0x13e; ++key)
        assert(fakeinput::Xbox360Code(1, key) == key);
    // Trigger numeric codes in other event domains (SYN, MSC, FF, etc.) are
    // unrelated and must survive the conversion untouched.
    for (unsigned short type : {0, 1, 2, 4, 5, 21})
        for (unsigned short code : {0, 4, 9, 10, 0x133, 0x134})
            assert(fakeinput::Xbox360Code(type, code) == code);
    std::puts("Xbox evdev button/axis compatibility tests passed");
}
