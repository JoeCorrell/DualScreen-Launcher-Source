#include "Panel.h"
#include "Ui.h"
#include "NamedArtCatalog.h"
#include "WeaponViews.h"
#include "../../DishonoredSecondScreen/src/Touch.h"
#include "../../DishonoredSecondScreen/src/PanelPresentation.h"
#include <windowsx.h>
#include <filesystem>
#include <memory>

namespace c3 {
namespace {
constexpr wchar_t ClassName[]=L"Crysis3SecondScreenPanel";
constexpr DWORD PumpMs=100;
std::atomic<bool> panelRunning{false};
using Logger=void (*)(const char*) noexcept;
void Log(Logger logger,const char* message) noexcept { if (logger) logger(message); }

struct Monitor { HMONITOR handle{}; RECT rect{}; bool primary{}; };
struct Monitors {
    std::array<Monitor,16> items{};
    size_t count{};
    bool invalid{};
};
bool SameRect(const RECT& a,const RECT& b) noexcept {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
bool Overlaps(const RECT& a,const RECT& b) noexcept {
    return a.left<b.right && b.left<a.right && a.top<b.bottom && b.top<a.bottom;
}
BOOL CALLBACK CollectMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM parameter) noexcept {
    auto& list=*reinterpret_cast<Monitors*>(parameter);
    MONITORINFO info{}; info.cbSize=sizeof(info);
    if (list.count==list.items.size() || !GetMonitorInfoW(monitor,&info)) {
        list.invalid=true; return FALSE;
    }
    const auto& r=info.rcMonitor;
    const int64_t width=int64_t(r.right)-r.left,height=int64_t(r.bottom)-r.top;
    if (width<=0 || height<=0 || width>16384 || height>16384) {
        list.invalid=true; return FALSE;
    }
    list.items[list.count++]={monitor,r,(info.dwFlags&MONITORINFOF_PRIMARY)!=0};
    return TRUE;
}
bool ReadMonitors(Monitors& list) noexcept {
    return EnumDisplayMonitors(nullptr,nullptr,CollectMonitor,reinterpret_cast<LPARAM>(&list)) &&
        !list.invalid && list.count>=2;
}
bool SafeSecondary(const Monitors& list,size_t index) noexcept {
    if (list.items[index].primary) return false;
    size_t primaryCount=0;
    for (size_t i=0;i<list.count;++i) {
        if (list.items[i].primary) ++primaryCount;
        // Mirrored/overlapping virtual displays are not a second surface.
        if (i!=index && Overlaps(list.items[index].rect,list.items[i].rect)) return false;
    }
    return primaryCount==1;
}
bool SelectMonitor(Monitor& selected) noexcept {
    Monitors list{};
    if (!ReadMonitors(list)) return false;
    int64_t best=INT64_MAX;
    for (size_t i=0;i<list.count;++i) if (SafeSecondary(list,i)) {
        const auto& r=list.items[i].rect;
        const int64_t dx=int64_t(r.right)-r.left-Width,dy=int64_t(r.bottom)-r.top-Height;
        const int64_t score=dx*dx+dy*dy;
        if (score<best) { selected=list.items[i]; best=score; }
    }
    return best!=INT64_MAX;
}
bool StillSafe(const Monitor& selected) noexcept {
    Monitors list{};
    if (!ReadMonitors(list)) return false;
    for (size_t i=0;i<list.count;++i)
        if (list.items[i].handle==selected.handle && SameRect(list.items[i].rect,selected.rect))
            return SafeSecondary(list,i);
    return false;
}
void StripCommands(Snapshot& s) noexcept {
    s.toggleArmor={}; s.toggleCloak={};
    for (auto& weapon:s.weapons.entries) {
        weapon.select={};
        for (auto& attachment:weapon.attachments.entries) attachment.select={};
    }
    for (auto& module:s.modules.entries) { module.toggle={}; module.purchase={}; }
}
struct Panel {
    Ui ui;
    dis::Canvas presentation;
    Snapshot snapshot{}, incoming{};
    dishonored::TouchSocket touch;
    Monitor monitor{};
    Fit fit{};
    Logger logger{};
    bool dirty{true}, closed{}, failed{}, mouseDown{}, commandLogged{};

    void Drop(const std::optional<Command>& command) noexcept {
        // Defense in depth if a later Ui revision introduces a command source.
        if (command && !commandLogged) {
            commandLogged=true; Log(logger,"Crysis panel: read-only command discarded");
        }
    }
    void Input(dishonored::Touch event,bool normalized) {
        using G=dishonored::Gesture;
        if (event.gesture==G::NextPage) { ui.PointerCancel(); ui.navigation.Next(1); }
        else if (event.gesture==G::PreviousPage) { ui.PointerCancel(); ui.navigation.Next(-1); }
        else if (event.gesture==G::Cancel) ui.PointerCancel();
        else if (event.gesture==G::Hold) return;
        else {
            float x=event.x,y=event.y;
            if (normalized) { x*=Width; y*=Height; }
            else if (!fit.Point(x,y)) { ui.PointerCancel(); return; }
            const uint64_t now=GetTickCount64();
            if (event.gesture==G::Down) ui.PointerDown(x,y,snapshot,now);
            else if (event.gesture==G::Move) ui.PointerMove(x,y);
            else if (event.gesture==G::Up) Drop(ui.PointerUp(x,y,snapshot,now));
            else if (event.gesture==G::Tap) Drop(ui.Tap(x,y,snapshot,now));
            else if (event.gesture==G::Pinch) { ui.PointerCancel(); ui.navigation.Pinch(x,y,event.factor); }
        }
        dirty=true;
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) noexcept {
    auto* p=reinterpret_cast<Panel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        p=static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));
    }
    if (!p) return DefWindowProcW(window,message,wp,lp);
    try {
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            p->fit=Fit::Into(LOWORD(lp),HIWORD(lp)); p->dirty=true; return 0;
        case WM_WINDOWPOSCHANGING: {
            auto* position=reinterpret_cast<WINDOWPOS*>(lp);
            // No external move/resize may turn this surface into a game overlay.
            position->flags|=SWP_NOACTIVATE|SWP_NOZORDER;
            const RECT& r=p->monitor.rect;
            if (!(position->flags&SWP_NOMOVE)) { position->x=r.left; position->y=r.top; }
            if (!(position->flags&SWP_NOSIZE)) { position->cx=r.right-r.left; position->cy=r.bottom-r.top; }
            return 0;
        }
        case WM_DISPLAYCHANGE:
            p->failed=true; p->ui.PointerCancel(); ShowWindow(window,SW_HIDE); return 0;
        case WM_LBUTTONDOWN: {
            p->mouseDown=true;
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT),TME_LEAVE,window,0};
            TrackMouseEvent(&tracking); // Leave notification, never pointer capture.
            p->Input({dishonored::Gesture::Down,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (p->mouseDown && (wp&MK_LBUTTON))
                p->Input({dishonored::Gesture::Move,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false);
            return 0;
        case WM_LBUTTONUP:
            if (p->mouseDown) p->Input({dishonored::Gesture::Up,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false);
            p->mouseDown=false; return 0;
        case WM_MOUSELEAVE: case WM_CANCELMODE: case WM_CAPTURECHANGED:
            p->mouseDown=false; p->ui.PointerCancel(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint);
            RECT client{}; GetClientRect(window,&client);
            const Rect& r=p->fit.bounds;
            const RECT fit{int(r.x),int(r.y),int(r.x+r.w),int(r.y+r.h)};
            // Never publish the old clear-then-copy black intermediate frame.
            // Prepare any letterboxing offscreen and update the window once.
            if(!dishonored::PresentPanelFrame(dc,p->ui.Canvas(),p->presentation,client,fit))
                Log(p->logger,"Crysis panel: completed-frame presentation failed");
            EndPaint(window,&paint); return 0;
        }
        case WM_CLOSE:
            p->closed=true; ShowWindow(window,SW_HIDE); return 0;
        case WM_DESTROY:
            p->closed=true; return 0; // Do not post WM_QUIT to the host thread.
        case WM_NCDESTROY:
            SetWindowLongPtrW(window,GWLP_USERDATA,0);
            return DefWindowProcW(window,message,wp,lp);
        default: return DefWindowProcW(window,message,wp,lp);
        }
    } catch (...) {
        p->failed=true; ShowWindow(window,SW_HIDE);
        Log(p->logger,"Crysis panel: window callback failed; panel hidden");
        return 0;
    }
}
struct RunningGuard { ~RunningGuard() { panelRunning.store(false,std::memory_order_release); } };
struct ClassGuard {
    HINSTANCE instance{}; bool owned{};
    ~ClassGuard() { if (owned) UnregisterClassW(ClassName,instance); }
};
struct WindowGuard {
    HWND handle{};
    ~WindowGuard() { if (handle && IsWindow(handle)) DestroyWindow(handle); }
};
} // namespace

bool RunPanel(Mailbox& mailbox,const std::wstring& assets,std::atomic<bool>& stopping,Logger logger) {
    if (panelRunning.exchange(true,std::memory_order_acq_rel)) {
        Log(logger,"Crysis panel: duplicate panel refused"); return false;
    }
    RunningGuard runningGuard;
    try {
        if (stopping.load(std::memory_order_acquire)) return true;
        auto panel=std::make_unique<Panel>(); panel->logger=logger;
        panel->ui.navigation.page=Page::Status;
        if (!SelectMonitor(panel->monitor)) {
            Log(logger,"Crysis panel: no real nonoverlapping secondary monitor; no overlay fallback"); return false;
        }
        if (!panel->ui.Create()) { Log(logger,"Crysis panel: canvas creation failed"); return false; }
        if (!panel->ui.artwork.suit.Load(std::filesystem::path(assets)/L"nanosuit.rgba"))
            Log(logger,"Crysis panel: optional Nanosuit artwork unavailable");
        const auto loadedIcons=LoadNamedArtwork(panel->ui.artwork,std::filesystem::path(assets));
        if(loadedIcons!=sizeof(NamedArtEntries)/sizeof(NamedArtEntries[0]))
            Log(logger,"Crysis panel: some optional named game icons unavailable");
        if(LoadWeaponViews(panel->ui.artwork,std::filesystem::path(assets))!=std::size(WeaponViewKeys))
            Log(logger,"Crysis panel: some optional cached model views unavailable");
        if (!panel->touch.Open()) { Log(logger,"Crysis panel: exclusive loopback touch unavailable"); return false; }
        ClassGuard type{GetModuleHandleW(nullptr),false};
        WNDCLASSW registration{};
        registration.lpfnWndProc=WindowProc; registration.hInstance=type.instance;
        registration.lpszClassName=ClassName;
        registration.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        type.owned=RegisterClassW(&registration)!=0;
        if (!type.owned) {
            WNDCLASSW existing{};
            if (GetLastError()!=ERROR_CLASS_ALREADY_EXISTS ||
                !GetClassInfoW(type.instance,ClassName,&existing) || existing.lpfnWndProc!=WindowProc) {
                Log(logger,"Crysis panel: window class registration failed"); return false;
            }
        }
        if (!StillSafe(panel->monitor)) { Log(logger,"Crysis panel: monitor topology changed before creation"); return false; }
        const RECT& r=panel->monitor.rect;
        WindowGuard window{CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,ClassName,
            L"Crysis 3 Dual-Screen Tactical Interface",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,
            nullptr,nullptr,type.instance,panel.get())};
        if (!window.handle) { Log(logger,"Crysis panel: window creation failed"); return false; }
        RECT client{}; GetClientRect(window.handle,&client);
        panel->fit=Fit::Into(client.right-client.left,client.bottom-client.top);
        panel->ui.Render(panel->snapshot,GetTickCount64());
        panel->dirty=false;
        ShowWindow(window.handle,SW_SHOWNOACTIVATE);
        Log(logger,"Crysis panel: read-only secondary surface started; actions disabled, render cap 10 Hz");
        bool previousCurrent=false;
        uint64_t lastTopologyCheck=GetTickCount64();
        while (!stopping.load(std::memory_order_acquire) && !panel->closed && !panel->failed) {
            // Sleep even under message/socket floods: bounded CPU and raster rate.
            Sleep(PumpMs);
            if (stopping.load(std::memory_order_acquire)) break;
            const uint64_t now=GetTickCount64();
            if (now-lastTopologyCheck>=1000) {
                lastTopologyCheck=now;
                if (!StillSafe(panel->monitor)) { panel->failed=true; ShowWindow(window.handle,SW_HIDE); break; }
            }
            if (mailbox.Read(panel->incoming) &&
                (panel->incoming.generation!=panel->snapshot.generation || panel->incoming.sequence!=panel->snapshot.sequence)) {
                if (panel->incoming.generation!=panel->snapshot.generation || panel->incoming.phase!=panel->snapshot.phase)
                    panel->ui.PointerCancel();
                panel->snapshot=panel->incoming;
                StripCommands(panel->snapshot); // Preserve all producer trust/phase flags.
                panel->dirty=true;
            }
            const bool current=Current(panel->snapshot,now);
            if (current!=previousCurrent) { panel->ui.PointerCancel(); panel->dirty=true; }
            previousCurrent=current;
            // Apply lifecycle invalidation before this pump's gestures, not after
            // a completed swipe when the next raster synchronizes navigation.
            panel->ui.navigation.Synchronize(panel->snapshot,now);
            panel->ui.navigation.mapReady=panel->ui.navigation.mapReady &&
                panel->ui.artwork.Map(panel->snapshot.map.artwork)!=nullptr;
            MSG message{};
            for (int count=0;count<64 && PeekMessageW(&message,window.handle,0,0,PM_REMOVE);++count) {
                if (message.message==WM_QUIT) { panel->closed=true; break; }
                DispatchMessageW(&message); // No keyboard translation/injection.
                if (panel->closed || panel->failed) break;
            }
            if (panel->closed || panel->failed) break;
            dishonored::Touch event{};
            const uint64_t rejected=panel->touch.Rejected();
            for (int count=0;count<32 && panel->touch.Receive(event);++count) panel->Input(event,true);
            if (panel->touch.Rejected()!=rejected) panel->ui.PointerCancel();
            mailbox.RequestPage(panel->ui.navigation.page);
            if (panel->dirty) {
                panel->ui.Render(panel->snapshot,GetTickCount64()); panel->dirty=false;
                InvalidateRect(window.handle,nullptr,FALSE);
                UpdateWindow(window.handle); // Paint now without activation or another raster pass.
            }
        }
        panel->ui.PointerCancel();
        ShowWindow(window.handle,SW_HIDE);
        Log(logger,panel->failed?"Crysis panel: stopped after surface/runtime failure":"Crysis panel: stopped cleanly");
        return !panel->failed;
    } catch (...) {
        Log(logger,"Crysis panel: initialization/runtime exception; panel released"); return false;
    }
}
} // namespace c3
