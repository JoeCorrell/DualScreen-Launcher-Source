#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "State.h"
#include "Touch.h"

namespace ac2ds {
struct UiState {
    Page page = Page::Map;
    uint64_t generation{}, selected{};
    std::wstring category;
    MapViewport map;
    float scroll{}, maximumScroll{}, velocity{}, lastX{}, lastY{};
    float detailScroll{}, maximumDetailScroll{}, categoryScroll{}, maximumCategoryScroll{};
    int dragRegion{};
    bool dragging{}, following = true;
    int pressedTab = -1;
    uint64_t pressedUntil{};
    uint64_t pressedAction{}, pressedActionUntil{};
    void TouchInput(const Touch&, const Snapshot&, StateStore&, uint64_t now);
    bool Animate(float elapsed, uint64_t now);
    void Synchronize(const Snapshot&);
};
void DrawAnimus(Gdiplus::Graphics&, const Snapshot&, UiState&, uint64_t now);
}
