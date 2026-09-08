#include <winsock2.h>
#include <windows.h>
#include <objidl.h>
#include <bcrypt.h>
#include <dinput.h>
#include <gdiplus.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include "AnimusUi.h"
#include "NativeBridge.h"
#include "AssetCache.h"
#include "MapCache.h"
#include "PanelSurface.h"
#include "MapRaster.h"
#include "MapSprites.h"
#include "VisualState.h"
#include "NativeAffinity.h"
#include "NativeCharacterPassTrace.h"

namespace {
HMODULE self{};
INIT_ONCE realOnce = INIT_ONCE_STATIC_INIT;
HMODULE realLibrary{};
std::atomic<bool> started{false};
std::wstring logPath;
std::mutex logMutex;
ac2ds::StateStore store;
ac2ds::assets::AssetCache* artwork{};
ac2ds::assets::MapCache* maps{};
constexpr wchar_t WindowClass[] = L"Ac2SecondScreenPanel";

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> guard(logMutex);
    if (logPath.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA metadata{};
    if (GetFileAttributesExW(logPath.c_str(), GetFileExInfoStandard, &metadata) && metadata.nFileSizeLow > 1024 * 1024)
        MoveFileExW(logPath.c_str(), (logPath + L".previous").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* file = _wfopen(logPath.c_str(), L"ab"); if (!file) return;
    std::fprintf(file, "[%llu] ", static_cast<unsigned long long>(GetTickCount64()));
    va_list args; va_start(args, format); std::vfprintf(file, format, args); va_end(args);
    std::fputc('\n', file); std::fclose(file);
}
bool VerifyExecutable() {
    wchar_t path[32768]{}; DWORD count = GetModuleFileNameW(nullptr, path, 32768);
    if (!count || count == 32768) return false;
    std::wstring executable(path); auto slash = executable.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    logPath = executable.substr(0, slash + 1) + L"AssassinsCreedIISecondScreen.log";
    if (_wcsicmp(executable.c_str() + slash + 1, L"AssassinsCreedIIGame.exe")) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || (size.QuadPart != 34693768 && size.QuadPart != 34701312)) {
        CloseHandle(file); return false;
    }
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{}; DWORD objectLength{}, returned{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength), &returned, 0) >= 0 && objectLength < 65536;
    std::vector<UCHAR> object(ok ? objectLength : 0), buffer(64 * 1024);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    while (ok) {
        DWORD read{}; if (!ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
    }
    UCHAR digest[32]{}; if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    char hex[65]{}; for (size_t i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    const bool originalBuild = size.QuadPart == 34693768 &&
        !std::strcmp(hex, "23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92");
    // Exact output of tools/patch_landing_contacts.py. All existing game RVAs
    // remain intact; only two landing-query calls and a new code section differ.
    const bool landingCandidate = size.QuadPart == 34701312 &&
        !std::strcmp(hex, "e0520f85b2cb3ac10ce59c8073cebd5853ab6395b3d2303f368b9e3203595abd");
    const bool landingRetry = size.QuadPart == 34701312 &&
        !std::strcmp(hex, "da55d92905936ecd79e287fc268ae4af59e242f8cbf6ab9b29a566847ff4f9a3");
    const bool landingRetryGeometry = size.QuadPart == 34701312 &&
        !std::strcmp(hex, "e680e6109c39828b4f5e842d2b53fae388638a36800bdb17b5e9eb127efefb39");
    const bool landingOrder = size.QuadPart == 34701312 &&
        !std::strcmp(hex, "89c7bbb81d44b5bed37b9e71302689742933e9732d918c1f1405619ebd58c088");
    ok = ok && (originalBuild || landingCandidate || landingRetry || landingRetryGeometry || landingOrder);
    Log("build sha256=%s accepted=%d; native action entry points require separate verification", hex, int(ok));
    if (ok && landingCandidate) Log("landing contact-search candidate recognized; live symptom resolution unverified");
    if (ok && landingRetry) Log("landing retry candidate recognized; native transition/damage path retained; live symptom resolution unverified");
    if (ok && landingRetryGeometry) Log("landing retry geometry-latch guard correction recognized; live symptom resolution unverified");
    if (ok && landingOrder) Log("landing contact-order candidate recognized; pre/post native landing checks and native cache refresh; live symptom resolution unverified");
    return ok;
}
struct Panel {
    struct Slide {
        ac2ds::PanelSurface previous{nullptr},next{nullptr};
        uint64_t started{},generation{};ac2ds::Phase phase{};int direction{};bool ready{};
    };
    std::unique_ptr<Slide> slide;
    ac2ds::UiState ui;
    ac2ds::PanelSurface surface{nullptr};
    ac2ds::PanelSurface background{nullptr};
    ac2ds::PanelSurface mapCanvas{nullptr};
    ac2ds::MapMotion mapMotion;
    ac2ds::MapSprites mapSprites;
    ac2ds::MapViewport renderedViewport;
    std::shared_ptr<const ac2ds::Snapshot> rendered;
    SOCKET socket = INVALID_SOCKET;
    uint64_t lastPaint{}, lastTick{}, generation{}, revision{}, artworkRevision{};
    uint64_t acceptedInput{}, rejectedInput{}, lastInputLog{};
    uint64_t mapDiagnosticGeneration{};
    uint64_t renderCount{}, renderTotal{}, renderMaximum{}, lastRenderLog{};
    uint64_t fullRenderCount{};
    bool dirty = true;
    explicit Panel(HWND) {
        ui.images = artwork;
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket != INVALID_SOCKET) {
            sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(7788);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            u_long nonblocking = 1;
            if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || ioctlsocket(socket, FIONBIO, &nonblocking)) {
                Log("input bind failed error=%d; leaving existing listener untouched", WSAGetLastError());
                closesocket(socket); socket = INVALID_SOCKET;
            }
        }
        Log("panel initialized buffer=%d renderer=direct-dib input=%d endpoint=127.0.0.1:7788 protocol=tap-duration-v2", int(bool(surface)), int(socket != INVALID_SOCKET));
    }
    ~Panel() {
        if (socket != INVALID_SOCKET) closesocket(socket);
    }
    void Tick(HWND window) {
        auto now = GetTickCount64(); auto snapshot = store.Read(); ui.Synchronize(*snapshot);
        if(slide && (slide->generation!=snapshot->generation || slide->phase!=snapshot->phase)){slide.reset();dirty=true;}
        auto pose=mapMotion.Update(*snapshot,now);
        bool poseChanged=pose.x!=ui.displayPose.x || pose.y!=ui.displayPose.y || pose.heading!=ui.displayPose.heading;
        ui.displayPose=pose;ui.smoothPose=true;
        ac2ds::affinity::Tick(now);
        ac2ds::character_trace::Tick(now,Log);
        if (!rendered || revision != snapshot->revision || generation != snapshot->generation)
            dirty |= !rendered || !ac2ds::SameVisualState(*rendered,*snapshot,ui.page,false);
        if(rendered && ui.page==ac2ds::Page::Map)
            dirty |= !ac2ds::SameSelectedMapDetails(*rendered,*snapshot,ui.selected);
        if (artwork) {
            auto updated = artwork->Revision(); dirty |= updated != artworkRevision; artworkRevision = updated;
        }
        revision = snapshot->revision; generation = snapshot->generation;
        if (snapshot->map && mapDiagnosticGeneration != generation) {
            mapDiagnosticGeneration = generation;
            Log("panel map generation=%llu playerXY=%d heading=%d x=%.6f y=%.6f angle=%.6f",
                static_cast<unsigned long long>(generation), int(snapshot->mapX && snapshot->mapY), int(snapshot->heading.has_value()),
                double(snapshot->mapX.value_or(-1)), double(snapshot->mapY.value_or(-1)), double(snapshot->heading.value_or(0)));
        }
        // Bounded drain: a hostile or faulty local sender cannot monopolize rendering.
        for (int i = 0; i < 48 && socket != INVALID_SOCKET; ++i) {
            char bytes[193]{}; sockaddr_in source{}; int sourceSize = sizeof(source);
            int count = recvfrom(socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&source), &sourceSize);
            if (count <= 0) break;
            if (source.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
            auto touch = ac2ds::ParseTouch(bytes, size_t(count));
            if (!touch) {
                ++rejectedInput;
                if (rejectedInput == 1 || now - lastInputLog >= 5000) {
                    Log("input rejected bytes=%d accepted=%llu rejected=%llu", count,
                        static_cast<unsigned long long>(acceptedInput), static_cast<unsigned long long>(rejectedInput));
                    lastInputLog = now;
                }
                continue;
            }
            ++acceptedInput;
            if (acceptedInput == 1 || now - lastInputLog >= 5000) {
                Log("input accepted kind=%d x=%.3f y=%.3f accepted=%llu rejected=%llu", int(touch->kind), touch->x, touch->y,
                    static_cast<unsigned long long>(acceptedInput), static_cast<unsigned long long>(rejectedInput));
                lastInputLog = now;
            }
            // Drain terminal gesture packets during the short visual transition;
            // they must not select an item on the incoming page.
            if(slide && slide->ready)continue;
            auto previous=ui.page;auto selected=ui.selected;bool following=ui.following;
            auto filter=ui.mapFilter;auto categoryScroll=ui.categoryScroll;
            ui.TouchInput(*touch,*snapshot,store,now);
            // Map motion changes the city viewport/overlay, not the page chrome.
            dirty |= previous!=ac2ds::Page::Map || ui.page!=previous || selected!=ui.selected ||
                following!=ui.following || filter!=ui.mapFilter || categoryScroll!=ui.categoryScroll;
            if(previous!=ui.page){
                slide=std::make_unique<Slide>();
                if(rendered && slide->previous && slide->next){
                    slide->previous.CopyFrom(surface);slide->direction=ui.pageDirection;
                    slide->generation=snapshot->generation;slide->phase=snapshot->phase;
                }else slide.reset();
                Log("page=%d navigation=swipe",int(ui.page));break;
            }
        }
        dirty |= ui.Animate(lastTick ? float(now - lastTick) / 1000 : 0, now); lastTick = now;
        if(slide && slide->ready){
            float fraction=ac2ds::PageSlideFraction(slide->started,now);
            ac2ds::SlidePages(surface.Pixels(),slide->previous.Pixels(),slide->next.Pixels(),slide->direction,fraction);
            InvalidateRect(window,nullptr,FALSE);lastPaint=now;
            if(fraction>=1){slide.reset();dirty=true;}
            return;
        }
        bool dna = ui.page == ac2ds::Page::Missions || ui.page == ac2ds::Page::Progress;
        auto viewport=ac2ds::DisplayMapViewport(*snapshot,ui);
        bool viewportChanged=false;
        bool playerMoved=false;
        if(ui.page==ac2ds::Page::Map) {
            viewportChanged=viewport.x!=renderedViewport.x || viewport.y!=renderedViewport.y || viewport.zoom!=renderedViewport.zoom;
            // Marker additions/removals and movement only redraw the overlay.
            // They must not rerasterize the full city and page every second.
            playerMoved=poseChanged || !rendered || !ac2ds::SameVisualState(*rendered,*snapshot,ui.page);
        }
        if ((!dirty && !viewportChanged && !playerMoved && !(dna && now-lastPaint>=100)) || !surface || !background || !mapCanvas) return;
        LARGE_INTEGER begin{},end{},frequency{}; QueryPerformanceCounter(&begin);
        if(dirty) {
            ++fullRenderCount;
            Gdiplus::Graphics graphics(background.Image()); ac2ds::DrawAnimus(graphics,*snapshot,ui,now,false,false);
            graphics.Flush(Gdiplus::FlushIntentionSync);
        }
        if(ui.page==ac2ds::Page::Map) {
            if(dirty || viewportChanged) {
                // RasterMap overwrites the entire map rectangle. Gesture-only
                // frames keep chrome and avoid another 5.1 MiB surface copy.
                if(dirty)mapCanvas.CopyFrom(background);
                if((snapshot->capabilities & ac2ds::Map) && snapshot->map)
                    ac2ds::RasterMap(mapCanvas.Pixels(),size_t(ac2ds::Width)*ac2ds::Height,*snapshot->map,viewport,background.Pixels());
            }
            surface.CopyFrom(mapCanvas);
            mapSprites.Draw(surface.Pixels(),size_t(ac2ds::Width)*ac2ds::Height,*snapshot,viewport,ui.mapFilter,ui.selected,ui.images);
        } else surface.CopyFrom(background);
        { Gdiplus::Graphics graphics(surface.Image()); ac2ds::DrawAnimusMotion(graphics, *snapshot, ui, now,false);
          graphics.Flush(Gdiplus::FlushIntentionSync); }
        if(slide){
            slide->next.CopyFrom(surface);slide->started=GetTickCount64();slide->ready=true;
            surface.CopyFrom(slide->previous);
        }
        QueryPerformanceCounter(&end); QueryPerformanceFrequency(&frequency);
        if (frequency.QuadPart > 0) {
            uint64_t micros = uint64_t(end.QuadPart-begin.QuadPart)*1000000/uint64_t(frequency.QuadPart);
            ++renderCount; renderTotal += micros; renderMaximum = std::max(renderMaximum,micros);
            if (!lastRenderLog) lastRenderLog=now;
            if (now-lastRenderLog >= 10000) {
                Log("panel render page=%d frames=%llu intervalMs=%llu meanUs=%llu maxUs=%llu full=%llu",int(ui.page),
                    static_cast<unsigned long long>(renderCount),static_cast<unsigned long long>(now-lastRenderLog),
                    static_cast<unsigned long long>(renderTotal/renderCount),static_cast<unsigned long long>(renderMaximum),
                    static_cast<unsigned long long>(fullRenderCount));
                renderCount=renderTotal=renderMaximum=fullRenderCount=0; lastRenderLog=now;
            }
        }
        rendered = snapshot;
        renderedViewport=viewport;
        dirty = false; lastPaint = now; InvalidateRect(window, nullptr, FALSE);
    }
};
LRESULT WindowProcImpl(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        panel = new(std::nothrow) Panel(window); if (!panel) return -1;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel)); SetTimer(window, 1, 16, nullptr); return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_TIMER && panel) { panel->Tick(window); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC target = BeginPaint(window, &ps); RECT client{}; GetClientRect(window, &client);
        if (panel) panel->surface.Present(target,client.right,client.bottom);
        EndPaint(window, &ps); return 0;
    }
    if (message == WM_DESTROY) { KillTimer(window, 1); SetWindowLongPtrW(window, GWLP_USERDATA, 0); delete panel; PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) noexcept {
    try { return WindowProcImpl(window, message, w, l); }
    catch (...) {
        // A UI allocation/rendering failure must never unwind into the game.
        KillTimer(window, 1); Log("panel stopped after an internal exception");
        if (message == WM_CREATE) return -1;
        if (message != WM_DESTROY) PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
}
BOOL CALLBACK Secondary(HMONITOR monitor, HDC, LPRECT, LPARAM value) {
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY)) {
        *reinterpret_cast<RECT*>(value) = info.rcMonitor; return FALSE;
    }
    return TRUE;
}
DWORD RunImpl() {
    if (!VerifyExecutable()) { Log("unsupported build: no window or hooks installed"); return 0; }
    ac2ds::affinity::Install(Log);
    wchar_t diagnosticExecutable[32768]{};
    const DWORD diagnosticLength=GetModuleFileNameW(nullptr,diagnosticExecutable,32768);
    if (diagnosticLength && diagnosticLength<32768) {
        const auto diagnosticPath=std::filesystem::path(diagnosticExecutable).parent_path()/L"AssassinsCreedIISecondScreen.ini";
        if (GetPrivateProfileIntW(L"Diagnostics",L"ModEnabled",1,diagnosticPath.c_str())==0) {
            Log("diagnostic control: ModEnabled=0; DirectInput forwarding and configured affinity retained; no native bridge, asset workers or panel");
            // The normal panel timer services the one delayed affinity repair.
            // Keep it in this background initialization thread for the control
            // test as well; the game's thread and input are never waited on.
            if (ac2ds::affinity::repairAt) {
                Sleep(10010);
                ac2ds::affinity::Tick(GetTickCount64());
            }
            return 0;
        }
    }
    ac2ds::Snapshot initial; initial.generation = 1; initial.revision = 1; initial.phase = ac2ds::Phase::Loading;
    initial.status = L"Awaiting synchronization"; initial.sampledAt = GetTickCount64(); store.Publish(std::move(initial));
    RECT monitor{}; EnumDisplayMonitors(nullptr, nullptr, Secondary, reinterpret_cast<LPARAM>(&monitor));
    if (monitor.right <= monitor.left || monitor.bottom <= monitor.top) { Log("no secondary monitor; top screen left untouched"); return 0; }
    if(diagnosticLength && diagnosticLength<32768){
        auto path=std::filesystem::path(diagnosticExecutable).parent_path()/L"AssassinsCreedIISecondScreen.ini";
        if(GetPrivateProfileIntW(L"Diagnostics",L"CharacterPassTrace",1,path.c_str())!=0)
            Log("character-pass trace hooks=%d; one bounded metadata capture; preview remains disabled",int(ac2ds::character_trace::Install()));
    }
    wchar_t executable[32768]{};
    DWORD pathLength = GetModuleFileNameW(nullptr, executable, 32768);
    if (pathLength && pathLength < 32768) {
        auto archives = std::make_shared<ac2ds::assets::GameArchives>(std::filesystem::path(executable).parent_path());
        // The proxy is pinned for process lifetime. Keep the bounded worker
        // alive with it; never join a thread from DllMain during Wine shutdown.
        artwork = new ac2ds::assets::AssetCache([archives](uint32_t id) { return archives->Load(id); },
            [](uint32_t id, const char* error) { Log("artwork id=%08lx unavailable: %s", static_cast<unsigned long>(id), error); });
        Log("artwork worker ready; on-demand original archives, 32MiB cache");
        auto directory=std::filesystem::path(executable).parent_path();
        maps=new ac2ds::assets::MapCache([directory](uint32_t id){return ac2ds::assets::LoadCityMap(directory,id);},
            [](uint32_t id,const char* message){Log("map artwork=%08lx %s",static_cast<unsigned long>(id),message);});
    }
    if (!ac2ds::InstallNativeBridge(store, Log, maps)) Log("native adapter unavailable; no game data exposed");
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa)) { Log("Winsock initialization failed"); return 0; }
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) { WSACleanup(); return 0; }
    WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = WindowProc; cls.hInstance = self;
    cls.lpszClassName = WindowClass; RegisterClassExW(&cls);
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, WindowClass,
        L"Assassin's Creed II Dual-Screen Animus", WS_POPUP, monitor.left, monitor.top,
        monitor.right - monitor.left, monitor.bottom - monitor.top, nullptr, nullptr, self, nullptr);
    if (window) {
        ShowWindow(window, SW_SHOWNOACTIVATE); MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    } else Log("window creation failed error=%lu", GetLastError());
    UnregisterClassW(WindowClass, self); Gdiplus::GdiplusShutdown(token); WSACleanup(); Log("panel shutdown"); return 0;
}
DWORD WINAPI Run(LPVOID) noexcept {
    try { return RunImpl(); }
    catch (...) { Log("initialization stopped after an internal exception"); return 0; }
}
BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{}; UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 13) return FALSE;
    lstrcatW(path, L"\\dinput8.dll"); realLibrary = LoadLibraryW(path);
    return realLibrary && realLibrary != self;
}
}
extern "C" HRESULT WINAPI Ac2DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    if (!InitOnceExecuteOnce(&realOnce, LoadReal, nullptr, nullptr)) return E_FAIL;
    using Function = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    FARPROC proc = GetProcAddress(realLibrary, "DirectInput8Create"); Function real{}; std::memcpy(&real, &proc, sizeof(real));
    if (!real) return E_FAIL;
    HRESULT result = real(instance, version, iid, output, outer);
    if (SUCCEEDED(result) && !started.exchange(true)) {
        HMODULE pinned{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&self), &pinned)) {
            HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
        }
    }
    return result;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self = module;
    return TRUE;
}
