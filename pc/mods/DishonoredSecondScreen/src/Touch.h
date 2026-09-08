#pragma once
#include <cstdint>
#include <string_view>

namespace dishonored {
enum class Gesture : uint8_t { Down, Move, Up, Tap, Hold, Pinch, Cancel, NextPage, PreviousPage };
struct Touch { Gesture gesture{}; float x{}, y{}, factor{1}; };

// Existing Skyrim/WEMU normalized loopback protocol. Parsing is independent
// of the socket so malformed coordinates can be verified offline.
bool ParseTouch(std::string_view packet, Touch& event) noexcept;
class TouchSocket {
public:
    TouchSocket() = default;
    TouchSocket(const TouchSocket&) = delete;
    TouchSocket& operator=(const TouchSocket&) = delete;
    ~TouchSocket();
    bool Open() noexcept;
    void Close() noexcept;
    // Nonblocking; call at most 32 times per panel pump. No input injection.
    bool Receive(Touch& event) noexcept;
    uint64_t Rejected() const noexcept { return rejected_; }
private:
    uintptr_t socket_{~uintptr_t(0)};
    bool winsock_{};
    uint64_t rejected_{};
};
} // namespace dishonored
