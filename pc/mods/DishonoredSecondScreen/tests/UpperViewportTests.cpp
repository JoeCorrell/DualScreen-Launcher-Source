// Win32 boundary simulation: exercise selection, lifecycle and placement using
// the production implementation without opening windows or needing the game.
#include "../src/UpperViewport.h"
#include <cassert>
#include <cstdio>
#include <vector>

namespace fake {
constexpr uintptr_t Procedure = 0x009ca440;
struct Window {
    DWORD thread{7}, process{12};
    bool visible{true}, iconic{}, enabled{true}, owned{};
    LONG_PTR style{WS_OVERLAPPEDWINDOW | WS_VISIBLE}, extended{};
    uintptr_t procedure{Procedure};
    RECT outer{320, 450, 1600, 1198}, client{0, 0, 1274, 716};
};
std::vector<Window*> windows;
RECT monitor{0, 0, 1280, 1800};
DWORD flags{}, error{};
unsigned positions{}, styles{}, enumerations{};
bool failMove{}, failStyle{}, failMonitor{};
Window& win(HWND h) { return *reinterpret_cast<Window*>(h); }
DWORD thread() { return 7; }
DWORD process() { return 12; }
DWORD owner(HWND h, DWORD* p) { *p=win(h).process; return win(h).thread; }
BOOL visible(HWND h) { return win(h).visible; }
BOOL iconic(HWND h) { return win(h).iconic; }
BOOL enabled(HWND h) { return win(h).enabled; }
HWND owned(HWND h, UINT) { return win(h).owned ? h : nullptr; }
LONG_PTR style(HWND h, int index) { return index == GWL_STYLE ? win(h).style : win(h).extended; }
ULONG_PTR procedure(HWND h, int) { return win(h).procedure; }
BOOL client(HWND h, RECT* r) { *r=win(h).client; return TRUE; }
BOOL outer(HWND h, RECT* r) { *r=win(h).outer; return TRUE; }
BOOL enumerate(WNDENUMPROC callback, LPARAM p) {
    ++enumerations;
    for(auto* w:windows) callback(reinterpret_cast<HWND>(w),p);
    return TRUE;
}
HMONITOR fromWindow(HWND, DWORD) { return reinterpret_cast<HMONITOR>(1); }
BOOL monitorInfo(HMONITOR, MONITORINFO* info) { info->rcMonitor=monitor; return !failMonitor; }
BOOL gui(DWORD t, GUITHREADINFO* info) { assert(t==7 || t==9); info->flags=flags; return TRUE; }
void setError(DWORD e) { error=e; }
DWORD getError() { return error; }
LONG_PTR setStyle(HWND h, int i, LONG_PTR s) {
    assert(i==GWL_STYLE); ++styles;
    if(failStyle) { error=ERROR_ACCESS_DENIED; return 0; }
    auto old=win(h).style; win(h).style=s; return old;
}
BOOL position(HWND h, HWND after, int x, int y, int w, int height, UINT f) {
    const bool asynchronous = owner(h,&error)!=thread();
    assert(!after && f==(SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_NOACTIVATE|(asynchronous ? SWP_ASYNCWINDOWPOS : SWP_FRAMECHANGED)));
    ++positions;
    if(failMove) return FALSE;
    win(h).outer={x,y,x+w,y+height}; win(h).client={0,0,w,height}; return TRUE;
}
void reset() {
    windows.clear();monitor={0,0,1280,1800};flags=error=positions=styles=enumerations=0;
    failMove=failStyle=failMonitor=false;
}
}
#undef GetWindowLongPtrW
#undef GetClassLongPtrW
#undef SetWindowLongPtrW
#define GetCurrentThreadId fake::thread
#define GetCurrentProcessId fake::process
#define GetWindowThreadProcessId fake::owner
#define IsWindowVisible fake::visible
#define IsIconic fake::iconic
#define IsWindowEnabled fake::enabled
#define GetWindow fake::owned
#define GetWindowLongPtrW fake::style
#define GetClassLongPtrW fake::procedure
#define GetClientRect fake::client
#define GetWindowRect fake::outer
#define EnumWindows fake::enumerate
#define MonitorFromWindow fake::fromWindow
#define GetMonitorInfoW fake::monitorInfo
#define GetGUIThreadInfo fake::gui
#define SetLastError fake::setError
#define GetLastError fake::getError
#define SetWindowLongPtrW fake::setStyle
#define SetWindowPos fake::position
#include "../src/UpperViewport.cpp"

int main() {
    using namespace dishonored;
    Bridge bridge;
    // Regression: centered 1274x716 client overlaps the panel at y=720.
    fake::reset(); fake::Window game; fake::windows={&game}; UpperViewport viewport;
    assert(!viewport.Tick(fake::Procedure,0,bridge)); assert(!fake::enumerations);
    bridge.stackedDesktop=true;
    assert(viewport.Tick(fake::Procedure,2000,bridge)); assert(fake::positions==1);
    assert(game.outer.top==0 && game.outer.bottom==720 && game.client.right==1280);
    assert(!(game.style & WS_CAPTION));
    assert(!viewport.Tick(fake::Procedure,2500,bridge)); assert(fake::enumerations==1);
    assert(!viewport.Tick(fake::Procedure,4000,bridge)); assert(fake::positions==1);
    // Recreated/save-load window must be rediscovered, with no cached HWND.
    fake::Window replacement; fake::windows={&replacement};
    assert(viewport.Tick(fake::Procedure,6000,bridge)); assert(fake::positions==2);
    Bridge::Event event{}; unsigned verified{};
    while(bridge.diagnostics.Pop(event)) if(event.revision==1) ++verified;
    assert(verified==2);
    // Never resize foreign/worker windows, a dialog, hidden/minimized windows,
    // a panel, unknown native window procedure or an ambiguous game client.
    for(unsigned mode=0;mode<13;++mode) {
        fake::reset(); fake::Window w, other; fake::windows={&w}; UpperViewport guard;
        switch(mode) {
            case 0:w.thread=9;break; case 1:w.process=13;break;
            case 2:w.visible=false;break; case 3:w.iconic=true;break;
            case 4:w.enabled=false;break; case 5:w.owned=true;break;
            case 6:w.style|=WS_CHILD;break; case 7:w.extended=WS_EX_TOOLWINDOW;break;
            case 8:w.procedure=1;break; case 9:fake::windows.push_back(&other);break;
            case 10:fake::monitor.bottom=720;break;
            case 11:fake::flags=GUI_INMOVESIZE;break; case 12:fake::failMonitor=true;break;
        }
        assert(!guard.Tick(fake::Procedure,0,bridge)); assert(!fake::positions && !fake::styles);
    }
    fake::reset();fake::Window failing;fake::windows={&failing};UpperViewport failure;
    fake::failMove=true;
    assert(failure.Tick(fake::Procedure,0,bridge)); // Yield sampling even on failure.
    assert(fake::positions==1);
    assert(!failure.Tick(fake::Procedure,1000,bridge)); assert(fake::positions==1);
    puts("PASS: overlap, throttling, HWND recreation, 13 safety exclusions and failed resize");
    fake::reset();fake::Window separate;separate.thread=9;separate.style=WS_POPUP|WS_VISIBLE;
    fake::windows={&separate};UpperViewport asynchronous;
    assert(asynchronous.Tick(fake::Procedure,0,bridge));assert(fake::positions==1 && fake::styles==0);
    assert(!asynchronous.Tick(fake::Procedure,2000,bridge));assert(fake::positions==1);
    puts("PASS: separate viewport owner uses asynchronous placement without style mutation");
}
