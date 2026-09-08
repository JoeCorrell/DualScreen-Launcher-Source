#pragma once
#include "Layout.h"
#include "Artwork.h"
#include "../../DeadIslandSecondScreen/src/Canvas.h"
#include <optional>

namespace c3 {
class Ui {
    struct Target { Rect bounds{}; Capability capability{}; };
    dis::Canvas canvas_;
    std::array<Target,48> targets_{};
    size_t targetCount_{};
    uint64_t nextRequest_{1};
    uint64_t renderedGeneration_{}, renderedSequence_{}, lastActionAt_{};
    Page renderedPage_{Page::Map};
    int renderedScroll_{};
    Capability pressed_{};
    uint64_t pressedGeneration_{};
    Page pressedPage_{Page::Map};
    int pressedScroll_{};
    const Target* Hit(float x,float y) const;
    void Panel(Rect box, const std::string& title);
    void Label(Rect box,const std::string& text,int size=22,dis::Color color={215,236,235});
    void Empty(Rect box,const std::string& message);
    void Button(Rect box,const std::string& label,const Capability& capability,const Snapshot& s,uint64_t now);
    void Meter(Rect box,const std::string& label,Number value,Number maximum);
public:
    ArtworkCatalog artwork;
    NavigationState navigation;
    bool Create() { return canvas_.Create(Width,Height); }
    dis::Canvas& Canvas() { return canvas_; }
    void Render(const Snapshot& s,uint64_t now);
    void PointerDown(float x,float y,const Snapshot& s,uint64_t now);
    void PointerMove(float x,float y) { navigation.Move(x,y); }
    void PointerCancel() { navigation.Cancel(); pressed_={}; }
    std::optional<Command> PointerUp(float x,float y,const Snapshot& s,uint64_t now);
    std::optional<Command> Tap(float x,float y,const Snapshot& s,uint64_t now);
};
} // namespace c3
