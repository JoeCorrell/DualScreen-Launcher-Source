#include "Panel.h"
#include "Ui.h"
#include "PanelPresentation.h"
#include <windowsx.h>
#include <algorithm>
#include <cstdio>

namespace dishonored {
namespace {
constexpr wchar_t PanelClass[] = L"DishonoredSecondScreenPanel";
// Stock WEMU recognizes this transport identity for private GDI window claiming
// and suppresses duplicate X11 mouse injection. No Skyrim game code is used.
constexpr wchar_t WemuPanelClass[] = L"DishonoredSecondScreenPanel.SkyrimSecondScreenPanel";
constexpr wchar_t PanelTitle[] = L"Dishonored Dual-Screen Journal";
struct Monitor {
    RECT rect{};
    bool found{};
    int score{-1};
    uint32_t count{};
    RECT primary{};
    bool tallDesktop{};
};
BOOL CALLBACK FindMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    auto& choice = *reinterpret_cast<Monitor*>(parameter);
    ++choice.count;
    if (info.dwFlags & MONITORINFOF_PRIMARY) { choice.primary = info.rcMonitor; return TRUE; }
    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width < 320 || height < 320 || width > 4096 || height > 4096) return TRUE;
    const int score = (width == 1240 && height == 1080) ? 100000 : 10000 - std::abs(width - 1240) - std::abs(height - 1080);
    if (score > choice.score) { choice.rect = info.rcMonitor; choice.found = true; choice.score = score; }
    return TRUE;
}
struct Panel {
    Ui ui;
    dis::Canvas presentation;
    HWND window{};
    TouchSocket touch;
    RECT fit{};
    bool repaint{true};
    bool monitorChanged{};
    std::array<uint64_t, 9> touches{};
    uint64_t outsideTouches{}, paintFailures{}, renders{}, maximumRenderMs{}, maximumPresentMs{}, maximumInputMs{};
    DWORD paintError{};
    explicit Panel(Bridge& bridge) : ui(bridge) {}
    void Resize(int width, int height) {
        const float scale = std::min(width / 1240.0f, height / 1080.0f);
        const int w = int(1240 * scale), h = int(1080 * scale);
        fit = {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
        repaint = true;
    }
    void Paint(HDC dc) {
        if (!ui.Canvas().ready()) return;
        const uint64_t started = GetTickCount64();
        RECT client{}; GetClientRect(window, &client);
        if (!PresentPanelFrame(dc, ui.Canvas(), presentation, client, fit)) {
            paintError = GetLastError(); ++paintFailures;
        }
        maximumPresentMs = std::max(maximumPresentMs, GetTickCount64() - started);
    }
    bool Render(uint64_t now) {
        const uint64_t started = GetTickCount64();
        const bool changed = ui.Render(now);
        // Rendering can consume part (or all) of a transition on Wine. Present
        // its current position, not a time-zero copy of the previous page.
        if (changed) ui.Render(GetTickCount64());
        if (changed) { ++renders; maximumRenderMs = std::max(maximumRenderMs, GetTickCount64() - started); }
        return changed;
    }
    void Input(Touch event, bool normalized) {
        RECT client{}; GetClientRect(window, &client);
        if (event.gesture != Gesture::Cancel && event.gesture != Gesture::NextPage && event.gesture != Gesture::PreviousPage) {
            float x = normalized ? event.x * client.right : event.x;
            float y = normalized ? event.y * client.bottom : event.y;
            if (fit.right <= fit.left || fit.bottom <= fit.top || x < fit.left || x >= fit.right || y < fit.top || y >= fit.bottom) {
                ++outsideTouches;
                if (event.gesture == Gesture::Up || event.gesture == Gesture::Move) ui.Cancel();
                return;
            }
            event.x = (x - fit.left) / (fit.right - fit.left);
            event.y = (y - fit.top) / (fit.bottom - fit.top);
        }
        const uint64_t started = GetTickCount64();
        if (ui.Input(event, started)) {
            if (uint32_t(event.gesture) < touches.size()) ++touches[uint32_t(event.gesture)];
            repaint = true;
        }
        maximumInputMs = std::max(maximumInputMs, GetTickCount64() - started);
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        panel = static_cast<Panel*>(create->lpCreateParams); panel->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel));
    }
    if (!panel) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_CLOSE: return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DISPLAYCHANGE:
            ShowWindow(window, SW_HIDE); panel->ui.Cancel(); panel->monitorChanged = true; return 0;
        case WM_SIZE: panel->Resize(LOWORD(lparam), HIWORD(lparam)); return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); panel->Paint(dc); EndPaint(window, &paint); return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE: {
            if (message == WM_MOUSEMOVE && !(wparam & MK_LBUTTON) && GetCapture() != window) return 0;
            Gesture kind = message == WM_LBUTTONDOWN ? Gesture::Down : message == WM_LBUTTONUP ? Gesture::Up : Gesture::Move;
            if (message == WM_LBUTTONDOWN) SetCapture(window);
            panel->Input({kind, float(GET_X_LPARAM(lparam)), float(GET_Y_LPARAM(lparam)), 1}, false);
            if (message == WM_LBUTTONUP && GetCapture() == window) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED: panel->ui.Cancel(); return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(window, GWLP_USERDATA, 0); panel->window = nullptr; break;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

bool RunPanel(Bridge& bridge, const std::wstring& assets, const std::wstring& configuration,
    std::atomic<bool>& stopping, LogFunction log) {
    Panel panel(bridge);
    const bool allowTallDesktop = GetPrivateProfileIntW(L"Panel", L"TallDesktopFallback", 1, configuration.c_str()) != 0;
    const bool wemuTransport = GetPrivateProfileIntW(L"Panel", L"WemuSkyrimTransport", 0, configuration.c_str()) != 0;
    const bool maintainUpper = GetPrivateProfileIntW(L"Panel", L"MaintainUpperViewport", 1, configuration.c_str()) != 0;
    const wchar_t* panelClass = wemuTransport ? WemuPanelClass : PanelClass;
    if (log && wemuTransport) log("[panel] WEMU compatibility identity enabled: Skyrim private GDI/UDP transport; Dishonored adapter and title retained");
    if (!panel.ui.Create(assets)) {
        if (log) {
            log("[panel] original assets/fonts or canvas initialization failed");
            char detail[1536]{};
            const auto& failure = panel.ui.Failure();
            const int length = int(std::min<std::size_t>(failure.size(), 480));
            const int written = WideCharToMultiByte(CP_UTF8, 0, failure.data(), length, detail, sizeof(detail) - 1, nullptr, nullptr);
            if (written > 0) { detail[written] = 0; log(detail); }
        }
        return false;
    }
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.lpfnWndProc = WindowProc;
    type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = panelClass;
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (log) log("[panel] window class registration failed");
        return false;
    }
    bool touchReady = panel.touch.Open();
    if (log) log(touchReady ? "[touch] isolated loopback receiver ready on 127.0.0.1:7788" : "[touch] loopback receiver unavailable; no game-input fallback");
    uint64_t nextMonitor{}, nextLog{}, previousRejected{}, previousSequence{};
    uint32_t previousDropped{};
    std::array<uint64_t, 9> previousTouches{};
    uint64_t previousOutside{}, previousPaintFailures{};
    RECT previousMonitor{};
    uint32_t previousMonitorCount = UINT32_MAX;
    bool previousMonitorFound{};
    bool previousTallDesktop{};
    RECT previousPrimary{}, reportedMonitor{};
    bool success = true;
    while (!stopping.load(std::memory_order_acquire)) {
        const uint64_t now = GetTickCount64();
        Bridge::Event diagnostic{};
        for (uint32_t events = 0; events < 32 && bridge.diagnostics.Pop(diagnostic); ++events) {
            if (!log) continue;
            constexpr const char* categories[] = {"lifecycle", "state", "domain", "action-submit", "action-result", "memory-fault", "sample-reads", "root-validation", "objective-markers", "map-discovery", "upper-viewport", "sample-phase", "map-projection"};
            char line[320]{};
            std::snprintf(line, sizeof(line), "[%s] sampledAt=%llu generation=%llu id=%llu revisionOrKey=%llu a=%d b=%d c=%d d=%d",
                categories[uint32_t(diagnostic.kind)], static_cast<unsigned long long>(diagnostic.at),
                static_cast<unsigned long long>(diagnostic.generation), static_cast<unsigned long long>(diagnostic.id),
                static_cast<unsigned long long>(diagnostic.revision), diagnostic.a, diagnostic.b, diagnostic.c, diagnostic.d);
            log(line);
        }
        if (now >= nextMonitor || panel.monitorChanged) {
            panel.monitorChanged = false;
            nextMonitor = now + 1000;
            Monitor monitor;
            EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&monitor));
            // Skyrim's panel has a configured (0,720,1240,1080) fallback
            // when Wine reports one tall desktop. Limit ours to that exact
            // verified Thor layout; a normal single 1280x720 game monitor
            // is not enough space and must never receive an overlapping UI.
            if (!monitor.found && allowTallDesktop && monitor.count == 1 &&
                monitor.primary.right - monitor.primary.left == 1280 &&
                monitor.primary.bottom - monitor.primary.top == 1800) {
                monitor.rect = {monitor.primary.left, monitor.primary.top + 720,
                    monitor.primary.left + 1240, monitor.primary.bottom};
                monitor.found = monitor.tallDesktop = true;
            }
            bridge.stackedDesktop.store(maintainUpper && monitor.tallDesktop &&
                monitor.primary.left == 0 && monitor.primary.top == 0, std::memory_order_release);
            if (log && (previousMonitorCount != monitor.count || previousMonitorFound != monitor.found ||
                previousTallDesktop != monitor.tallDesktop || !EqualRect(&previousPrimary, &monitor.primary) ||
                !EqualRect(&reportedMonitor, &monitor.rect))) {
                char line[288]{};
                std::snprintf(line, sizeof(line), "[panel] monitors=%u primary=%ld,%ld %ldx%ld secondary=%u bounds=%ld,%ld %ldx%ld",
                    unsigned(monitor.count), monitor.primary.left, monitor.primary.top,
                    monitor.primary.right - monitor.primary.left, monitor.primary.bottom - monitor.primary.top,
                    unsigned(monitor.found), monitor.rect.left, monitor.rect.top,
                    monitor.rect.right - monitor.rect.left, monitor.rect.bottom - monitor.rect.top);
                log(line);
                if (monitor.tallDesktop) log("[panel] Skyrim-style tall-desktop lower rectangle selected: 1240x1080 below the 720px gameplay region");
                if (!monitor.found) log("[panel] waiting for a secondary Wine monitor; Android's second physical display alone is insufficient");
                previousMonitorCount = monitor.count; previousMonitorFound = monitor.found;
                previousTallDesktop = monitor.tallDesktop;
                previousPrimary = monitor.primary; reportedMonitor = monitor.rect;
            }
            if (!monitor.found) {
                if (panel.window && IsWindowVisible(panel.window)) { ShowWindow(panel.window, SW_HIDE); panel.ui.Cancel(); }
            } else if (!panel.window) {
                panel.window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, panelClass, PanelTitle,
                    WS_POPUP, monitor.rect.left, monitor.rect.top, monitor.rect.right - monitor.rect.left,
                    monitor.rect.bottom - monitor.rect.top, nullptr, nullptr, type.hInstance, &panel);
                if (!panel.window) { if (log) log("[panel] lower window creation failed"); success = false; break; }
                panel.ui.Render(now);
                HDC dc = GetDC(panel.window); if (dc) { panel.Paint(dc); ReleaseDC(panel.window, dc); }
                ShowWindow(panel.window, SW_SHOWNOACTIVATE); previousMonitor = monitor.rect;
                if (log) log("[panel] lower window initialized; stacked game placement maintained on its owning thread");
            } else {
                if (!EqualRect(&previousMonitor, &monitor.rect)) {
                    SetWindowPos(panel.window, nullptr, monitor.rect.left, monitor.rect.top,
                        monitor.rect.right - monitor.rect.left, monitor.rect.bottom - monitor.rect.top,
                        SWP_NOACTIVATE | SWP_NOZORDER);
                    previousMonitor = monitor.rect;
                }
                if (!IsWindowVisible(panel.window)) ShowWindow(panel.window, SW_SHOWNOACTIVATE);
            }
        }
        bool changed{};
        const auto& snapshot = bridge.snapshots.Read(changed);
        panel.ui.Update(snapshot, now);
        if (panel.Render(now)) panel.repaint = true; // Rebuild hit targets before consuming touches.
        MSG message{};
        for (uint32_t messages = 0; messages < 128 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++messages) {
            if (message.message == WM_QUIT) { stopping.store(true, std::memory_order_release); break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Touch event{};
        for (uint32_t packets = 0; packets < 128 && panel.touch.Receive(event); ++packets) {
            if (panel.window && IsWindowVisible(panel.window)) panel.Input(event, true);
        }
        const uint64_t afterInput = GetTickCount64();
        panel.ui.Animate(afterInput);
        if (panel.Render(afterInput)) panel.repaint = true;
        if (panel.repaint && panel.window && IsWindowVisible(panel.window)) {
            InvalidateRect(panel.window, nullptr, FALSE); UpdateWindow(panel.window); panel.repaint = false;
        }
        if (log && now >= nextLog) {
            nextLog = now + 5000;
            char timing[224]{};
            std::snprintf(timing, sizeof(timing), "[panel-timing] renders=%llu maxRenderMs=%llu maxPresentMs=%llu maxInputMs=%llu",
                static_cast<unsigned long long>(panel.renders), static_cast<unsigned long long>(panel.maximumRenderMs),
                static_cast<unsigned long long>(panel.maximumPresentMs), static_cast<unsigned long long>(panel.maximumInputMs));
            log(timing); panel.renders = panel.maximumRenderMs = panel.maximumPresentMs = panel.maximumInputMs = 0;
            if (previousTouches != panel.touches || previousOutside != panel.outsideTouches) {
                char line[384]{};
                std::snprintf(line, sizeof(line), "[touch] down=%llu move=%llu up=%llu tap=%llu hold=%llu pinch=%llu cancel=%llu next=%llu previous=%llu outside=%llu",
                    static_cast<unsigned long long>(panel.touches[0]), static_cast<unsigned long long>(panel.touches[1]),
                    static_cast<unsigned long long>(panel.touches[2]), static_cast<unsigned long long>(panel.touches[3]),
                    static_cast<unsigned long long>(panel.touches[4]), static_cast<unsigned long long>(panel.touches[5]),
                    static_cast<unsigned long long>(panel.touches[6]), static_cast<unsigned long long>(panel.touches[7]),
                    static_cast<unsigned long long>(panel.touches[8]), static_cast<unsigned long long>(panel.outsideTouches));
                log(line); previousTouches = panel.touches; previousOutside = panel.outsideTouches;
            }
            if (previousPaintFailures != panel.paintFailures) {
                char line[128]{};
                std::snprintf(line, sizeof(line), "[render] completed-frame presentation failures=%llu lastError=%lu",
                    static_cast<unsigned long long>(panel.paintFailures), static_cast<unsigned long>(panel.paintError));
                log(line); previousPaintFailures = panel.paintFailures;
            }
            const uint32_t dropped = bridge.droppedDiagnostics.load(std::memory_order_relaxed);
            if (dropped != previousDropped) {
                char line[96]{}; std::snprintf(line, sizeof(line), "[logging] bounded queue dropped %lu records",
                    static_cast<unsigned long>(dropped - previousDropped)); log(line); previousDropped = dropped;
            }
            if (previousSequence != snapshot.sequence || previousRejected != panel.touch.Rejected()) {
                char line[240]{};
                std::snprintf(line, sizeof(line), "[panel] generation=%llu sequence=%llu live=%u touchRejected=%llu",
                    static_cast<unsigned long long>(snapshot.generation), static_cast<unsigned long long>(snapshot.sequence),
                    unsigned(bridge::Fresh(snapshot, now)), static_cast<unsigned long long>(panel.touch.Rejected()));
                log(line); previousSequence = snapshot.sequence; previousRejected = panel.touch.Rejected();
            }
        }
        // nCount=0 waits only for input; no handle array is dereferenced.
        // Cppcheck's Windows model marks pHandles unconditionally nonnull.
        // cppcheck-suppress nullPointer
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    panel.touch.Close();
    bridge.stackedDesktop.store(false, std::memory_order_release);
    if (panel.window) DestroyWindow(panel.window);
    UnregisterClassW(panelClass, type.hInstance);
    if (log) log("[panel] clean shutdown");
    return success;
}
} // namespace dishonored
