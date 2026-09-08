#include "UpperViewport.h"

namespace dishonored {
namespace {
struct Candidate { uintptr_t procedure{}; HWND window{}; unsigned count{}; DWORD owner{}; unsigned localWindows{}, procedureMatches{}; };
BOOL CALLBACK Find(HWND window, LPARAM parameter) {
    auto& candidate = *reinterpret_cast<Candidate*>(parameter);
    DWORD process{};
    const DWORD owner = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !owner) return TRUE;
    ++candidate.localWindows;
    if (uintptr_t(GetClassLongPtrW(window, GCLP_WNDPROC)) != candidate.procedure) return TRUE;
    ++candidate.procedureMatches;
    if (!IsWindowVisible(window) ||
        IsIconic(window) || !IsWindowEnabled(window) || GetWindow(window, GW_OWNER) ||
        (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        uintptr_t(GetClassLongPtrW(window, GCLP_WNDPROC)) != candidate.procedure) return TRUE;
    RECT client{};
    if (!GetClientRect(window, &client) || client.right < 320 || client.bottom < 200) return TRUE;
    candidate.window = window; candidate.owner = owner; ++candidate.count;
    return TRUE;
}
void Record(Bridge& bridge, uint64_t now, HWND window, uint64_t status, const RECT& outer, const RECT& client) {
    bridge.Record({Bridge::EventKind::UpperViewport, now, 0, uintptr_t(window), status,
        outer.left, outer.top, client.right, client.bottom});
}
}
bool UpperViewport::Tick(uintptr_t expectedWindowProcedure, uint64_t now, Bridge& bridge) noexcept {
    if (now < nextCheck_) return false;
    nextCheck_ = now + 2000;
    if (!bridge.stackedDesktop.load(std::memory_order_acquire)) return false;
    Candidate candidate{expectedWindowProcedure, nullptr, 0};
    EnumWindows(Find, reinterpret_cast<LPARAM>(&candidate));
    // Fail closed on ambiguous windows; never choose an error dialog or panel
    // by title/area, and never cache a potentially recycled HWND.
    if (candidate.count != 1) {
        if (now >= nextDiagnostic_) {
            nextDiagnostic_ = now + 10000;
            bridge.Record({Bridge::EventKind::UpperViewport, now, 0, GetCurrentThreadId(), 3,
                int32_t(candidate.count), int32_t(candidate.localWindows), int32_t(candidate.procedureMatches), 0});
        }
        return false;
    }
    const HWND window = candidate.window;
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONULL), &monitor) ||
        monitor.rcMonitor.left != 0 || monitor.rcMonitor.top != 0 ||
        monitor.rcMonitor.right != 1280 || monitor.rcMonitor.bottom != 1800) return false;
    GUITHREADINFO thread{}; thread.cbSize = sizeof(thread);
    if (!GetGUIThreadInfo(candidate.owner, &thread) ||
        (thread.flags & (GUI_INMENUMODE | GUI_INMOVESIZE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE))) return false;
    RECT outer{}, client{};
    if (!GetWindowRect(window, &outer) || !GetClientRect(window, &client)) return false;
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    constexpr LONG_PTR borders = WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
        WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (outer.left == 0 && outer.top == 0 && outer.right == 1280 && outer.bottom == 720 &&
        client.right == 1280 && client.bottom == 720 && !(style & borders)) {
        if (now >= nextDiagnostic_) {
            nextDiagnostic_ = now + 10000;
            Record(bridge, now, window, 1, outer, client);
        }
        return false;
    }
    Record(bridge, now, window, 0, outer, client); // Before correction.
    if (candidate.owner != GetCurrentThreadId()) {
        // UE3 may own its viewport on a different thread from player ticks.
        // Never synchronously send a resize/style message across those threads:
        // the owner may be waiting on the game/render synchronization fence.
        // A borderless native viewport needs only an asynchronous move/resize.
        if (style & borders) {
            if (now >= nextDiagnostic_) {
                nextDiagnostic_ = now + 10000;
                Record(bridge, now, window, 6, outer, client);
            }
            return false;
        }
        const bool queued = SetWindowPos(window, nullptr, 0, 0, 1280, 720,
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
        Record(bridge, now, window, queued ? 4 : 2, outer, client);
        // This is a queued request, not verification. The next tick checks
        // the actual client rectangle before reporting status 1.
        nextDiagnostic_ = now + 10000;
        return true;
    }
    const LONG_PTR borderless = (style & ~borders) | WS_POPUP;
    if (borderless != style) {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(window, GWL_STYLE, borderless) && GetLastError() != ERROR_SUCCESS) {
            Record(bridge, now, window, 2, outer, client); return false;
        }
    }
    // WFC's proven owner-thread path: UE3 processes its own WM_SIZE and
    // swap-chain recreation. Keep the game out of the panel's y=720..1800
    // region; host-side scaling cannot fix overlap inside Wine.
    const bool moved = SetWindowPos(window, nullptr, 0, 0, 1280, 720,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
    const bool verified = moved && GetWindowRect(window, &outer) && GetClientRect(window, &client) &&
        outer.left == 0 && outer.top == 0 && outer.right == 1280 && outer.bottom == 720 &&
        client.right == 1280 && client.bottom == 720;
    Record(bridge, now, window, verified ? 1 : 2, outer, client);
    nextDiagnostic_ = now + 10000;
    // Always yield the remainder of sampling after a resize attempt, including
    // failed/asynchronous Wine transitions. Recheck only on a later slice.
    return true;
}
}
