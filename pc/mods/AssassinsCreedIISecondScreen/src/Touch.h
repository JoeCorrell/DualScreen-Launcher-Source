#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string_view>

namespace ac2ds {
enum class TouchKind { Tap, Hold, Down, Move, Up, Cancel, Pinch, Next, Previous };
struct Touch { TouchKind kind{}; float x{}, y{}, value{}; };
inline std::optional<Touch> ParseTouch(const char* bytes, size_t length) {
    if (!bytes || !length || length > 192 || std::memchr(bytes, 0, length)) return {};
    std::array<std::string_view, 5> words{}; size_t count = 0, start = 0;
    auto space = [](char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
    while (start < length) {
        while (start < length && space(bytes[start])) ++start;
        if (start == length) break;
        size_t end = start; while (end < length && !space(bytes[end])) ++end;
        if (count == words.size()) return {};
        words[count++] = {bytes + start, end - start}; start = end;
    }
    if (count == 2 && words[0] == "page") {
        if (words[1] == "next") return Touch{TouchKind::Next};
        if (words[1] == "previous") return Touch{TouchKind::Previous};
    }
    if (count < 3 || count > 4) return {};
    Touch t;
    if (words[0] == "tap") t.kind = TouchKind::Tap;
    else if (words[0] == "hold") t.kind = TouchKind::Hold;
    else if (words[0] == "down") t.kind = TouchKind::Down;
    else if (words[0] == "move") t.kind = TouchKind::Move;
    else if (words[0] == "up") t.kind = TouchKind::Up;
    else if (words[0] == "cancel") t.kind = TouchKind::Cancel;
    else if (words[0] == "pinch") t.kind = TouchKind::Pinch;
    else return {};
    auto number = [](std::string_view s, float& out) {
        if (s.empty() || s.size() > 32) return false;
        char buffer[33]{}; std::memcpy(buffer, s.data(), s.size());
        char* end{}; errno = 0; out = std::strtof(buffer, &end);
        return !errno && end == buffer + s.size() && std::isfinite(out);
    };
    if (!number(words[1], t.x) || !number(words[2], t.y) || t.x < 0 || t.x > 1 || t.y < 0 || t.y > 1) return {};
    if (count == 4 && !number(words[3], t.value)) return {};
    if (t.kind == TouchKind::Pinch) {
        if (count != 4 || t.value < 0.25f || t.value > 4.f) return {};
    // CompanionGuestPanelMotionRouter sends elapsed seconds for BOTH terminal
    // tap and hold packets. GamePanelPresentation omits it for ordinary taps.
    // Accept both launcher encodings; never interpret duration as another action.
    } else if (count == 4 && ((t.kind != TouchKind::Hold && t.kind != TouchKind::Tap) || t.value < 0 || t.value > 60)) return {};
    return t;
}

struct MapViewport {
    float zoom = 1, x = 0.5f, y = 0.5f;
    void Clamp() {
        if (!std::isfinite(zoom)) zoom = 1;
        if (!std::isfinite(x)) x = .5f;
        if (!std::isfinite(y)) y = .5f;
        zoom = std::clamp(zoom, 1.f, 8.f);
        float edge = 0.5f / zoom;
        x = std::clamp(x, edge, 1 - edge); y = std::clamp(y, edge, 1 - edge);
    }
    void Pan(float dx, float dy) {
        if (!std::isfinite(dx) || !std::isfinite(dy)) return;
        x -= dx / zoom; y -= dy / zoom; Clamp();
    }
    void Pinch(float scale, float anchorX, float anchorY) {
        if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(anchorX) || !std::isfinite(anchorY)) return;
        float next = std::clamp(zoom * scale, 1.f, 8.f);
        x += (anchorX - .5f) * (1 / zoom - 1 / next);
        y += (anchorY - .5f) * (1 / zoom - 1 / next);
        zoom = next; Clamp();
    }
};
}
