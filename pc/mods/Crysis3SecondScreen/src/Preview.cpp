// Offline development harness, NOT a game payload and not installed by build.
#include "Ui.h"
#include "NamedArtCatalog.h"
#include "ReadOnlyTrial.h"
#include "WeaponViews.h"
#include "../../DishonoredSecondScreen/src/Touch.h"
#include "../../DishonoredSecondScreen/src/PanelPresentation.h"
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
struct Preview {
    c3::Ui ui;
    dis::Canvas presentation;
    c3::Snapshot snapshot;
    dishonored::TouchSocket touch;
    c3::Fit fit;
    bool dirty{true};
    void Input(dishonored::Touch event, bool normalized) {
        using G=dishonored::Gesture;
        if (event.gesture==G::NextPage) ui.navigation.Next(1);
        else if (event.gesture==G::PreviousPage) ui.navigation.Next(-1);
        else if (event.gesture==G::Cancel) ui.PointerCancel();
        else {
            float x=event.x,y=event.y;
            if (normalized) { x*=c3::Width; y*=c3::Height; }
            else if (!fit.Point(x,y)) { ui.PointerCancel(); return; }
            if (event.gesture==G::Down) ui.PointerDown(x,y,snapshot,GetTickCount64());
            else if (event.gesture==G::Move) ui.PointerMove(x,y);
            else if (event.gesture==G::Up) ui.PointerUp(x,y,snapshot,GetTickCount64());
            else if (event.gesture==G::Tap) ui.Tap(x,y,snapshot,GetTickCount64());
            else if (event.gesture==G::Pinch) ui.navigation.Pinch(x,y,event.factor);
        }
        dirty=true;
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto* p=reinterpret_cast<Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if (message==WM_NCCREATE) {
        p=static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));
    }
    if (!p) return DefWindowProcW(window,message,wp,lp);
    switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: p->fit=c3::Fit::Into(LOWORD(lp),HIWORD(lp)); p->dirty=true; return 0;
        case WM_LBUTTONDOWN: p->Input({dishonored::Gesture::Down,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false); return 0;
        case WM_MOUSEMOVE:
            if (wp & MK_LBUTTON) p->Input({dishonored::Gesture::Move,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false);
            return 0;
        case WM_LBUTTONUP: p->Input({dishonored::Gesture::Up,float(GET_X_LPARAM(lp)),float(GET_Y_LPARAM(lp))},false); return 0;
        case WM_CANCELMODE: case WM_CAPTURECHANGED: p->ui.PointerCancel(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(window,&ps);
            RECT client{}; GetClientRect(window,&client);
            const auto& r=p->fit.bounds;
            const RECT fit{int(r.x),int(r.y),int(r.x+r.w),int(r.y+r.h)};
            dishonored::PresentPanelFrame(dc,p->ui.Canvas(),p->presentation,client,fit);
            EndPaint(window,&ps); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wp,lp);
    }
}
bool Save(c3::Ui& ui,const std::filesystem::path& path) {
    Gdiplus::Bitmap bitmap(c3::Width,c3::Height,PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    HDC dc=graphics.GetHDC();
    BitBlt(dc,0,0,c3::Width,c3::Height,ui.Canvas().dc(),0,0,SRCCOPY);
    graphics.ReleaseHDC(dc);
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    return bitmap.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;
}
}
int main(int argc,char** argv) {
    if (argc<2 || (std::string(argv[1])!="--render" && std::string(argv[1])!="--render-sample" && std::string(argv[1])!="--window")) {
        std::cerr<<"Offline harness: --render DIRECTORY, --render-sample DIRECTORY, or --window [--touch]\n"; return 2;
    }
    Gdiplus::GdiplusStartupInput startup; ULONG_PTR token{};
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) return 3;
    int result=0;
    {
        auto p=std::make_unique<Preview>();
        // Only a fixed, local build asset. No game/UDP-derived path is opened.
        p->ui.artwork.suit.Load(std::filesystem::path(argv[0]).parent_path()/"assets/nanosuit.rgba");
        c3::LoadNamedArtwork(p->ui.artwork,std::filesystem::path(argv[0]).parent_path()/"assets");
        c3::LoadWeaponViews(p->ui.artwork,std::filesystem::path(argv[0]).parent_path()/"assets");
        if(std::string(argv[1])=="--render-sample") {
            // Explicit offline visual-test fixture, not compiled into dinput8.
            std::cout<<"SYNTHETIC UI REVIEW ONLY; these sample values are not game telemetry\n";
            auto& s=p->snapshot; s.readOnlyTrial=true; s.generation=1; s.sequence=1;
            s.sampledAt=GetTickCount64(); s.phase=c3::Phase::Gameplay;
            s.health={1000,true}; s.maxHealth={1000,true}; s.energy={75,true}; s.maxEnergy={100,true};
            s.armor={false,true}; s.cloak={true,true};
            c3::research::OwnedWeaponList owned{}; owned.count=4;
            const char* names[]={"NoWeapon","SCAR","Hammer","NanoBow"};
            for(size_t i=0;i<4;++i) { owned.weapons[i].entityId=uint32_t(i+1);
                std::snprintf(owned.weapons[i].className.data(),64,"%s",names[i]); }
            owned.weapons[2].equipped=true; c3::TrialOwnedWeapons(s,owned);
            c3native::NativeSuitModules modules{}; modules.count=4; modules.upgradePoints=4;
            const char* moduleIds[]={"BulletProof","Endurance","StrengthModule","SensorPackage"};
            const char* moduleClasses[]={"Armor","Speed","Power","Stealth"};
            for(size_t i=0;i<4;++i) { std::snprintf(modules.modules[i].id.data(),32,"%s",moduleIds[i]);
                std::snprintf(modules.modules[i].moduleClass.data(),32,"%s",moduleClasses[i]);
                modules.modules[i].installed=i<2; modules.modules[i].cost=uint32_t(i+1); }
            c3::TrialSuitModules(s,modules);
        }
        if (!p->ui.Create()) result=4;
        else if (std::string(argv[1])=="--render" || std::string(argv[1])=="--render-sample") {
            if (argc!=3) result=2;
            else {
                const std::filesystem::path output(argv[2]); std::filesystem::create_directories(output);
                for (int i=0;i<int(c3::Page::Count);++i) {
                    if(std::string(argv[1])=="--render-sample") p->snapshot.sampledAt=GetTickCount64();
                    p->ui.navigation.page=c3::Page(i); p->ui.Render(p->snapshot,GetTickCount64());
                    auto destination=output/(std::to_string(i+1)+".png");
                    if (!Save(p->ui,destination)) { result=5; break; }
                    std::cout<<destination.string()<<'\n';
                }
            }
        } else {
            if (argc==3 && std::string(argv[2])=="--touch" && !p->touch.Open()) {
                std::cerr<<"Loopback touch unavailable; no game input fallback\n"; result=6;
            }
            WNDCLASSW type{}; type.lpfnWndProc=WindowProc; type.hInstance=GetModuleHandleW(nullptr);
            type.lpszClassName=L"Crysis3OfflineUiReview"; type.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
            RegisterClassW(&type);
            // Explicit offline preview only. No window claiming, topmost flag,
            // focus calls, key injection, game hooks, or game file mutations.
            HWND window=CreateWindowExW(WS_EX_NOACTIVATE,type.lpszClassName,L"Crysis 3 - OFFLINE UI REVIEW (not connected)",
                WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,930,850,nullptr,nullptr,type.hInstance,p.get());
            if (!window) result=7;
            else {
                ShowWindow(window,SW_SHOWNOACTIVATE);
                bool running=true;
                while (running) {
                    MSG msg{};
                    while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                        if (msg.message==WM_QUIT) { running=false; break; }
                        TranslateMessage(&msg); DispatchMessageW(&msg);
                    }
                    dishonored::Touch event{};
                    for (int i=0;i<32 && p->touch.Receive(event);++i) p->Input(event,true);
                    if (p->dirty) { p->ui.Render(p->snapshot,GetTickCount64()); p->dirty=false; InvalidateRect(window,nullptr,FALSE); }
                    MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);
                }
            }
        }
    }
    Gdiplus::GdiplusShutdown(token); return result;
}
