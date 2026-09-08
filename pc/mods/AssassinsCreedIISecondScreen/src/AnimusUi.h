#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "State.h"
#include "Touch.h"
#include "MapMotion.h"
#include "PageSwipe.h"

namespace ac2ds {
struct UiState {
    ImageSource* images{}; // UI-thread requests carry owned IDs, never engine pointers.
    Page page = Page::Map;
    uint64_t generation{}, selected{}, selectedSequence{};
    std::wstring category;
    MapViewport map;
    MapPose displayPose;
    bool smoothPose=false;
    float scroll{}, maximumScroll{}, velocity{}, lastX{}, lastY{};
    float detailScroll{}, maximumDetailScroll{}, categoryScroll{}, maximumCategoryScroll{};
    int dragRegion{};
    bool dragging{}, following = true;
    uint32_t mapFilter{};
    PageSwipe swipe;
    int pageDirection{};
    uint64_t pageTransition{};
    uint64_t pressedAction{}, pressedActionUntil{};
    void TouchInput(const Touch&, const Snapshot&, StateStore&, uint64_t now);
    bool Animate(float elapsed, uint64_t now);
    void Synchronize(const Snapshot&);
};
MapViewport DisplayMapViewport(const Snapshot&,const UiState&);
void DrawAnimus(Gdiplus::Graphics&, const Snapshot&, UiState&, uint64_t now,bool motion=true,bool mapRaster=true);
void DrawAnimusMotion(Gdiplus::Graphics&,const Snapshot&,const UiState&,uint64_t now,bool mapIcons=true);
}
