#include "../src/Touch.h"
#include <cstdio>
#include <cstring>

int main() {
    using namespace dishonored;
    struct Valid { const char* packet; Gesture kind; };
    // Captured packet forms from installed WEMU's GuestPanelMotionRouter and
    // NativePanelInput DEX, plus the launcher's continuous-contact protocol.
    const Valid valid[] = {
        {"tap 0.25 0.75 0.12", Gesture::Tap},
        {"hold 0.25 0.75 0.8", Gesture::Hold},
        {"tap 0.25 0.75", Gesture::Tap},
        {"select 0.25 0.75", Gesture::Tap},
        {"down 0.25 0.75", Gesture::Down},
        {"move 0.30 0.65", Gesture::Move},
        {"up 0.30 0.65", Gesture::Up},
        {"pinch 0.5 0.5 1.05", Gesture::Pinch},
        {"cancel", Gesture::Cancel},
        {"page next", Gesture::NextPage},
        {"page previous", Gesture::PreviousPage},
    };
    const char* invalid[] = {
        "tap 0.25 0.75 -1", "tap 0.25 0.75 nan", "tap 0.25 0.75 0.1 junk",
        "tap -0.1 0.5", "tap 0.5 inf", "tap 1.01 0.5", "tap 0.5",
        "pinch 0.5 0.5", "pinch 0.5 0.5 0", "pinch 0.5 0.5 11",
        "move 0.5 0.5 0.1", "fire 0.5 0.5", "", "cancel junk",
    };
    Touch event{};
    for (const auto& test : valid) {
        if (!ParseTouch(test.packet, event) || event.gesture != test.kind) {
            std::fprintf(stderr, "Rejected valid packet: %s\n", test.packet); return 1;
        }
    }
    for (const auto* packet : invalid) {
        if (ParseTouch(packet, event)) {
            std::fprintf(stderr, "Accepted invalid packet: %s\n", packet); return 1;
        }
    }
    const char embedded[] = "tap 0.5 0.5\0fire";
    if (ParseTouch(std::string_view(embedded, sizeof(embedded) - 1), event)) return 1;
    std::puts("26 touch protocol cases passed; no device input sent.");
}
