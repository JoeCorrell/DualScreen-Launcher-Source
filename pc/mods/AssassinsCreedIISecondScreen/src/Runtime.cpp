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

namespace {
HMODULE self{};
INIT_ONCE realOnce = INIT_ONCE_STATIC_INIT;
HMODULE realLibrary{};
std::atomic<bool> started{false};
std::wstring logPath;
std::mutex logMutex;
ac2ds::StateStore store;
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
    if (!GetFileSizeEx(file, &size) || size.QuadPart != 34693768) { CloseHandle(file); return false; }
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
    ok = ok && !std::strcmp(hex, "23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92");
    Log("build sha256=%s accepted=%d; native actions disabled", hex, int(ok));
    return ok;
}
struct Panel {
    ac2ds::UiState ui;
    HDC dc{}; HBITMAP bitmap{}; HGDIOBJ old{};
    SOCKET socket = INVALID_SOCKET;
    uint64_t lastPaint{}, lastTick{}, generation{}, revision{};
    bool dirty = true;
    explicit Panel(HWND window) {
        HDC screen = GetDC(window); dc = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = ac2ds::Width; info.bmiHeader.biHeight = -ac2ds::Height;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels{}; bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        ReleaseDC(window, screen); if (dc && bitmap) old = SelectObject(dc, bitmap);
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
        Log("panel initialized buffer=%d input=%d", int(bitmap != nullptr), int(socket != INVALID_SOCKET));
    }
    ~Panel() {
        if (socket != INVALID_SOCKET) closesocket(socket);
        if (dc && old) SelectObject(dc, old); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
    }
    void Tick(HWND window) {
        auto now = GetTickCount64(); auto snapshot = store.Read(); ui.Synchronize(*snapshot);
        dirty |= revision != snapshot->revision || generation != snapshot->generation;
        revision = snapshot->revision; generation = snapshot->generation;
        // Bounded drain: a hostile or faulty local sender cannot monopolize rendering.
        for (int i = 0; i < 48 && socket != INVALID_SOCKET; ++i) {
            char bytes[193]{}; sockaddr_in source{}; int sourceSize = sizeof(source);
            int count = recvfrom(socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&source), &sourceSize);
            if (count <= 0) break;
            if (source.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
            auto touch = ac2ds::ParseTouch(bytes, size_t(count)); if (!touch) continue;
            auto previous = ui.page; ui.TouchInput(*touch, *snapshot, store, now); dirty = true;
            if (previous != ui.page) Log("page=%d", int(ui.page));
        }
        dirty |= ui.Animate(lastTick ? float(now - lastTick) / 1000 : 0, now); lastTick = now;
        bool dna = ui.page == ac2ds::Page::Missions || ui.page == ac2ds::Page::Progress;
        if (dna && now - lastPaint >= 100) dirty = true;
        if (!dirty || !dc || !bitmap) return;
        { Gdiplus::Graphics graphics(dc); ac2ds::DrawAnimus(graphics, *snapshot, ui, now); }
        dirty = false; lastPaint = now; InvalidateRect(window, nullptr, FALSE);
    }
};
LRESULT WindowProcImpl(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CREATE) {
        panel = new(std::nothrow) Panel(window); if (!panel) return -1;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panel)); SetTimer(window, 1, 33, nullptr); return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_TIMER && panel) { panel->Tick(window); return 0; }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC target = BeginPaint(window, &ps); RECT client{}; GetClientRect(window, &client);
        if (panel && panel->bitmap) { SetStretchBltMode(target, HALFTONE);
            StretchBlt(target, 0, 0, client.right, client.bottom, panel->dc, 0, 0, ac2ds::Width, ac2ds::Height, SRCCOPY); }
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
    ac2ds::Snapshot initial; initial.generation = 1; initial.revision = 1; initial.phase = ac2ds::Phase::Loading;
    initial.status = L"Awaiting synchronization"; initial.sampledAt = GetTickCount64(); store.Publish(std::move(initial));
    RECT monitor{}; EnumDisplayMonitors(nullptr, nullptr, Secondary, reinterpret_cast<LPARAM>(&monitor));
    if (monitor.right <= monitor.left || monitor.bottom <= monitor.top) { Log("no secondary monitor; top screen left untouched"); return 0; }
    if (!ac2ds::InstallNativeBridge(store, Log)) Log("native adapter unavailable; no game data exposed");
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
