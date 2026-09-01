// The dxgi.dll the game loads instead of the system one.
//
// DXHRDC.exe resolves its graphics entry points by name at run time -- there is
// no dxgi or d3d11 in its import table, only LoadLibraryA("d3d11.dll") and
// LoadLibraryA("dxgi.dll") inside the device probe at 0x0054BE20. The
// application directory is searched first, so a dxgi.dll sitting beside the
// executable is loaded in the system one's place and every call arrives here.
//
// Two callers matter and they arrive in this order:
//
//   1. d3d11.dll, whose own import table needs exactly one symbol from dxgi --
//      CreateDXGIFactory2. It is resolved by the loader while d3d11 is being
//      mapped, which is before the game has run a line of its own code.
//   2. The game, through GetProcAddress(dxgi, "CreateDXGIFactory").
//
// Everything is forwarded to the real library in System32 with the signature
// spelled out, rather than by a .def forwarder: a forwarder would have to name
// its target module, and this module is called dxgi, so it would forward to
// itself.

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "Log.h"
#include "Probe.h"

namespace {

HMODULE g_real = nullptr;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;

// The system library, by absolute path. LoadLibraryW(L"dxgi.dll") from in here
// would find this module again -- it is the one in the application directory,
// which is the whole reason it is being called.
HMODULE Real() {
  if (!g_lock_ready) return nullptr;
  EnterCriticalSection(&g_lock);
  if (!g_real) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 16) {
      wcscat(path, L"\\dxgi.dll");
      g_real = LoadLibraryW(path);
    }
    if (!g_real) DXS_ERR("[proxy] could not load the system dxgi.dll");
  }
  const HMODULE real = g_real;
  LeaveCriticalSection(&g_lock);
  return real;
}

FARPROC Proc(const char* name) {
  HMODULE m = Real();
  return m ? GetProcAddress(m, name) : nullptr;
}

}  // namespace

extern "C" {

HRESULT __stdcall CreateDXGIFactory(REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory"));
  if (!fn) return E_FAIL;
  HRESULT hr = fn(riid, out);
  if (SUCCEEDED(hr) && out && *out) dxs::ProbeOnFactory(static_cast<IUnknown*>(*out));
  return hr;
}

HRESULT __stdcall CreateDXGIFactory1(REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory1"));
  if (!fn) return E_FAIL;
  HRESULT hr = fn(riid, out);
  if (SUCCEEDED(hr) && out && *out) dxs::ProbeOnFactory(static_cast<IUnknown*>(*out));
  return hr;
}

HRESULT __stdcall CreateDXGIFactory2(UINT flags, REFIID riid, void** out) {
  using Fn = HRESULT(__stdcall*)(UINT, REFIID, void**);
  auto fn = reinterpret_cast<Fn>(Proc("CreateDXGIFactory2"));
  if (!fn) {
    // Windows 7 without the platform update has no CreateDXGIFactory2. d3d11
    // imports it regardless, so answering through the 1.0 entry point keeps a
    // machine like that working instead of failing at load.
    using Fn1 = HRESULT(__stdcall*)(REFIID, void**);
    auto fn1 = reinterpret_cast<Fn1>(Proc("CreateDXGIFactory1"));
    if (!fn1) return E_FAIL;
    HRESULT hr = fn1(riid, out);
    if (SUCCEEDED(hr) && out && *out) dxs::ProbeOnFactory(static_cast<IUnknown*>(*out));
    return hr;
  }
  HRESULT hr = fn(flags, riid, out);
  if (SUCCEEDED(hr) && out && *out) dxs::ProbeOnFactory(static_cast<IUnknown*>(*out));
  return hr;
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

// The DXGID3D10* group is private between d3d11 and dxgi. The signatures are
// not documented; these are the shapes the loader needs to get the arguments
// and the stack cleanup right, and nothing here inspects them.
HRESULT __stdcall DXGID3D10CreateDevice(HMODULE module, void* factory, void* adapter,
                                        UINT flags, void* unknown, void** device) {
  using Fn = HRESULT(__stdcall*)(HMODULE, void*, void*, UINT, void*, void**);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10CreateDevice"));
  return fn ? fn(module, factory, adapter, flags, unknown, device) : E_FAIL;
}

HRESULT __stdcall DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
  using Fn = HRESULT(__stdcall*)(void*, void*, void*, void*, void*);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10CreateLayeredDevice"));
  return fn ? fn(a, b, c, d, e) : E_FAIL;
}

SIZE_T __stdcall DXGID3D10GetLayeredDeviceSize(const void* layers, UINT count) {
  using Fn = SIZE_T(__stdcall*)(const void*, UINT);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10GetLayeredDeviceSize"));
  return fn ? fn(layers, count) : 0;
}

HRESULT __stdcall DXGID3D10RegisterLayers(const void* layers, UINT count) {
  using Fn = HRESULT(__stdcall*)(const void*, UINT);
  auto fn = reinterpret_cast<Fn>(Proc("DXGID3D10RegisterLayers"));
  return fn ? fn(layers, count) : E_FAIL;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      // This runs under the loader lock, while d3d11.dll is still being mapped.
      // Nothing that could take another lock belongs here -- no LoadLibrary, no
      // file I/O, no thread that expects the process to be initialised. All of
      // that waits for the first call that arrives from outside.
      DisableThreadLibraryCalls(instance);
      InitializeCriticalSection(&g_lock);
      g_lock_ready = true;
      dxs::ProbeStage();
      break;
    case DLL_PROCESS_DETACH:
      // At process termination Windows is tearing down COM, the graphics
      // driver, and other DLLs in an unspecified order. Calling into any of
      // them from here is unsafe and unnecessary because the address space is
      // going away. Keep explicit FreeLibrary cleanup for development only.
      if (!reserved) dxs::ProbeShutdown();
      break;
    default:
      break;
  }
  return TRUE;
}
