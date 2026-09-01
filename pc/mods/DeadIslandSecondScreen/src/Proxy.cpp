// The dxgi.dll Dead Island loads instead of the system one.
//
// rd3d11_x64_rwdi.dll imports d3d11.dll and dxgi.dll *statically*, so unlike
// Deus Ex there is no LoadLibrary to intercept -- the loader resolves them
// while the renderer module is being mapped, and the application directory is
// searched first. A dxgi.dll beside DeadIslandGame.exe is therefore loaded in
// the system one's place before the renderer has run a line of code.
//
// Everything is forwarded to the real library in System32 with the signature
// spelled out. A .def forwarder cannot be used: it would have to name its
// target module, and this module is called dxgi.

#include <windows.h>
#include <dxgi.h>

#include "Log.h"

namespace dis { void ModOnDxgiUsed(); }

namespace {
HMODULE g_real = nullptr;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;

HMODULE Real() {
  if (g_real) return g_real;
  if (!g_lock_ready) return nullptr;
  EnterCriticalSection(&g_lock);
  if (!g_real) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 16) {
      wcscat(path, L"\\dxgi.dll");
      g_real = LoadLibraryW(path);
    }
  }
  LeaveCriticalSection(&g_lock);
  return g_real;
}
FARPROC Proc(const char* name) { HMODULE m = Real(); return m ? GetProcAddress(m, name) : nullptr; }
}  // namespace

extern "C" {

HRESULT __stdcall CreateDXGIFactory(REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory"));
  if (!fn) return E_FAIL;
  dis::ModOnDxgiUsed();
  return fn(riid, out);
}

HRESULT __stdcall CreateDXGIFactory1(REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory1"));
  if (!fn) return E_FAIL;
  dis::ModOnDxgiUsed();
  return fn(riid, out);
}

HRESULT __stdcall CreateDXGIFactory2(UINT flags, REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(UINT, REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory2"));
  if (!fn) {
    using Fn1 = HRESULT(__stdcall*)(REFIID, void**);
    auto fn1 = reinterpret_cast<Fn1>(Proc("CreateDXGIFactory1"));
    if (!fn1) return E_FAIL;
    dis::ModOnDxgiUsed();
    return fn1(riid, out);
  }
  dis::ModOnDxgiUsed();
  return fn(flags, riid, out);
}

HRESULT __stdcall DXGIGetDebugInterface1(UINT flags, REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(UINT, REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("DXGIGetDebugInterface1"));
  return fn ? fn(flags, riid, out) : E_NOINTERFACE;
}

HRESULT __stdcall DXGIDeclareAdapterRemovalSupport() {
  using Fn = HRESULT(__stdcall*)();
  auto fn = reinterpret_cast<Fn>(Proc("DXGIDeclareAdapterRemovalSupport"));
  return fn ? fn() : E_NOTIMPL;
}

HRESULT __stdcall DXGID3D10CreateDevice(HMODULE m, void* f, void* a, UINT fl, void* u, void** d) {
  using Fn = HRESULT(__stdcall*)(HMODULE, void*, void*, UINT, void*, void**);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10CreateDevice"));
  return fn ? fn(m, f, a, fl, u, d) : E_FAIL;
}
HRESULT __stdcall DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
  using Fn = HRESULT(__stdcall*)(void*, void*, void*, void*, void*);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10CreateLayeredDevice"));
  return fn ? fn(a, b, c, d, e) : E_FAIL;
}
SIZE_T __stdcall DXGID3D10GetLayeredDeviceSize(const void* l, UINT c) {
  using Fn = SIZE_T(__stdcall*)(const void*, UINT);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10GetLayeredDeviceSize"));
  return fn ? fn(l, c) : 0;
}
HRESULT __stdcall DXGID3D10RegisterLayers(const void* l, UINT c) {
  using Fn = HRESULT(__stdcall*)(const void*, UINT);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10RegisterLayers"));
  return fn ? fn(l, c) : E_FAIL;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    // Under the loader lock, while rd3d11 is still being mapped. Nothing that
    // could take another lock belongs here.
    DisableThreadLibraryCalls(instance);
    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;
  }
  // Do not tear down D3D, windows, hooks, or logging from PROCESS_DETACH.
  // DllMain runs under the loader lock and those operations can load modules,
  // send messages, or take locks owned by another thread. This DXGI proxy is a
  // static process dependency and is never hot-unloaded; on normal process
  // termination Windows reclaims every one of those resources safely.
  return TRUE;
}
