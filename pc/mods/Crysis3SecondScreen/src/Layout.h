#pragma once
#include "Model.h"

namespace c3 {
struct Rect {
    float x{}, y{}, w{}, h{};
    bool Contains(float px, float py) const {
        return std::isfinite(px) && std::isfinite(py) && px >= x && py >= y && px < x+w && py < y+h;
    }
};
constexpr Rect Content{24, 82, 1192, 938};
constexpr Rect MapArea{40, 138, 1160, 712};
constexpr size_t VisibleModules=4, VisibleObjectives=5, VisibleStatistics=6;
constexpr Rect WeaponPanelBounds(int index) { return {40,98+float(index)*460,1160,438}; }
constexpr Rect ArmorButton{532,166,310,80};
constexpr Rect CloakButton{864,166,316,80};
constexpr Rect StatusArmorButton{60,268,532,72};
constexpr Rect StatusCloakButton{642,268,538,72};
constexpr Rect SuitFigurePanel{40,306,340,698};
constexpr Rect SuitFigureArea{60,354,300,600};
constexpr size_t AttachmentsPerPanel=3;
struct WeaponPanel { size_t weapon{}, attachmentStart{}; bool valid{}; };
inline size_t WeaponPanelCount(const List<Weapon,8>& weapons) {
    size_t count=0;
    for (size_t i=0;i<weapons.size();++i)
        count+=std::max<size_t>(1,(weapons.entries[i].attachments.size()+AttachmentsPerPanel-1)/AttachmentsPerPanel);
    return count;
}
inline WeaponPanel FindWeaponPanel(const List<Weapon,8>& weapons,size_t index) {
    for (size_t i=0;i<weapons.size();++i) {
        const size_t count=std::max<size_t>(1,(weapons.entries[i].attachments.size()+AttachmentsPerPanel-1)/AttachmentsPerPanel);
        if (index<count) return {i,index*AttachmentsPerPanel,true};
        index-=count;
    }
    return {};
}
struct Fit {
    Rect bounds{}; float scale{};
    static Fit Into(int width, int height) {
        if (width <= 0 || height <= 0) return {};
        float s = std::min(width/float(Width), height/float(Height));
        return {{(width-Width*s)/2, (height-Height*s)/2, Width*s, Height*s}, s};
    }
    bool Point(float& x, float& y) const {
        if (scale <= 0 || !bounds.Contains(x,y)) return false;
        x = (x-bounds.x)/scale; y = (y-bounds.y)/scale; return true;
    }
};
struct MapView {
    Rect viewport{MapArea};
    float zoom{1}, centerX{0.5f}, centerY{0.5f};
    void Clamp() {
        float half = 0.5f/zoom;
        centerX = std::clamp(centerX, half, 1-half);
        centerY = std::clamp(centerY, half, 1-half);
    }
    void Pan(float dx, float dy) {
        if (!std::isfinite(dx) || !std::isfinite(dy)) return;
        if (viewport.w<=0 || viewport.h<=0) return;
        centerX -= dx/(viewport.w*zoom); centerY -= dy/(viewport.h*zoom); Clamp();
    }
    void Pinch(float x, float y, float factor) {
        if (!viewport.Contains(x,y) || !std::isfinite(factor) || factor < 0.1f || factor > 10) return;
        const float nx=(x-viewport.x)/viewport.w-0.5f, ny=(y-viewport.y)/viewport.h-0.5f;
        float next = std::clamp(zoom*factor, 1.0f, 8.0f);
        centerX += nx/zoom-nx/next; centerY += ny/zoom-ny/next; zoom=next; Clamp();
    }
    std::array<float,2> Project(float x, float y) const {
        return {viewport.x+viewport.w*(0.5f+(x-centerX)*zoom),
                viewport.y+viewport.h*(0.5f+(y-centerY)*zoom)};
    }
};
class NavigationState {
    bool down_{};
    float startX_{}, startY_{}, lastX_{}, lastY_{};
    bool movedBeyondTap_{};
    uint64_t generation_{};
    Phase phase_{Phase::Unverified};
    bool current_{};
public:
    Page page{Page::Map};
    MapView map{};
    std::array<int,size_t(Page::Count)> scroll{};
    bool mapReady{};
    void Cancel() { down_ = false; }
    void Synchronize(const Snapshot& s, uint64_t now) {
        mapReady = Current(s,now) && s.map.calibrated && s.map.artwork;
        if (generation_ != s.generation) {
            generation_ = s.generation; map = {}; scroll = {}; Cancel();
        }
        const bool current=Current(s,now);
        if (phase_!=s.phase || current_!=current) Cancel();
        phase_=s.phase; current_=current;
    }
    void Next(int direction) {
        constexpr int count=int(Page::Count);
        page=Page((int(page)+(direction%count)+count)%count); Cancel();
    }
    void Pinch(float x,float y,float factor) {
        Cancel(); // A two-finger gesture must not finish as a tap/page swipe.
        if (page==Page::Map && mapReady) map.Pinch(x,y,factor);
    }
    void Down(float x,float y) {
        down_=std::isfinite(x) && std::isfinite(y); movedBeyondTap_=false;
        startX_=lastX_=x; startY_=lastY_=y;
    }
    void Move(float x,float y) {
        if (!down_) return;
        if (!std::isfinite(x) || !std::isfinite(y)) { Cancel(); return; }
        movedBeyondTap_=movedBeyondTap_ || std::abs(x-startX_)>=20 || std::abs(y-startY_)>=20;
        if (page == Page::Map && mapReady && map.viewport.Contains(startX_,startY_)) map.Pan(x-lastX_,y-lastY_);
        lastX_=x; lastY_=y;
    }
    bool Up(float x,float y) {
        if (!down_) return false;
        down_=false;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        float dx=x-startX_,dy=y-startY_;
        // Map body reserves dragging; header/footer swipes still change pages.
        if (!(page==Page::Map && mapReady && map.viewport.Contains(startX_,startY_)) &&
            std::abs(dx)>100 && std::abs(dx)>std::abs(dy)*1.5f) { Next(dx<0?1:-1); return false; }
        if (Content.Contains(startX_,startY_) && page!=Page::Map && std::abs(dy)>70 && std::abs(dy)>std::abs(dx)) {
            auto& row = scroll[int(page)]; row=std::clamp(row+(dy<0?1:-1),0,63); return false;
        }
        return !movedBeyondTap_ && std::abs(dx)<20 && std::abs(dy)<20;
    }
};
} // namespace c3
