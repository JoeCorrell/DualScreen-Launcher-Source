#include <windows.h>
#include <dxgi.h>
#include <cwchar>
#include <cstring>

namespace dis { void ModOnDxgiUsed(); }

extern "C" void InitSKSESteamLoader();

namespace {
HMODULE g_real = nullptr;
INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;
DWORD g_real_load_error = ERROR_SUCCESS;
volatile LONG g_real_failure_logged = 0;

BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH] = {};
  UINT n = GetSystemDirectoryW(path, MAX_PATH);
  if (n && n < MAX_PATH - 16) {
    wcscat(path, L"\\dxgi.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) g_real_load_error = GetLastError();
  } else {
    g_real_load_error = GetLastError();
  }
  // Returning FALSE leaves INIT_ONCE incomplete, allowing a later factory
  // call to retry instead of permanently poisoning every DXGI export.
  return g_real != nullptr;
}

FARPROC ResolveProc(const char* name) {
  const BOOL loaded =
      InitOnceExecuteOnce(&g_real_once, LoadReal, nullptr, nullptr);
  if ((!loaded || !g_real) &&
      InterlockedCompareExchange(&g_real_failure_logged, 1, 0) == 0) {
    wchar_t message[192] = {};
    swprintf(message, sizeof(message) / sizeof(message[0]),
             L"SkyrimSecondScreen: could not load system32\\dxgi.dll "
             L"(error %lu); DXGI initialization will retry.\n",
             static_cast<unsigned long>(g_real_load_error));
    OutputDebugStringW(message);
  }
  return g_real ? GetProcAddress(g_real, name) : nullptr;
}

template <typename Fn>
Fn Proc(const char* name) {
  // GetProcAddress intentionally returns the untyped FARPROC ABI. Copying its
  // representation into the exact export signature avoids a direct
  // incompatible-function cast while preserving the Windows x64 contract.
  static_assert(sizeof(Fn) == sizeof(FARPROC),
                "Windows function pointers must match FARPROC size");
  const FARPROC address = ResolveProc(name);
  Fn function = nullptr;
  std::memcpy(&function, &address, sizeof(function));
  return function;
}
}

extern "C" {
HRESULT __stdcall CreateDXGIFactory(REFIID iid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = Proc<Fn>("CreateDXGIFactory");
  if (!fn) return E_FAIL;
  dis::ModOnDxgiUsed();
  return fn(iid, out);
}
HRESULT __stdcall CreateDXGIFactory1(REFIID iid, void** out) {
  using Fn = HRESULT(__stdcall*)(REFIID, void**);
  auto fn = Proc<Fn>("CreateDXGIFactory1");
  if (!fn) return E_FAIL;
  dis::ModOnDxgiUsed();
  return fn(iid, out);
}
HRESULT __stdcall CreateDXGIFactory2(UINT flags, REFIID iid, void** out) {
  using Fn = HRESULT(__stdcall*)(UINT, REFIID, void**);
  auto fn = Proc<Fn>("CreateDXGIFactory2");
  if (fn) { dis::ModOnDxgiUsed(); return fn(flags, iid, out); }
  return CreateDXGIFactory1(iid, out);
}
HRESULT __stdcall DXGIGetDebugInterface1(UINT flags, REFIID iid, void** out) {
  using Fn = HRESULT(__stdcall*)(UINT, REFIID, void**);
  auto fn = Proc<Fn>("DXGIGetDebugInterface1");
  return fn ? fn(flags, iid, out) : E_NOINTERFACE;
}
HRESULT __stdcall DXGIReportAdapterConfiguration(void* configuration) {
  // This export is intentionally undocumented but present on current Windows
  // DXGI. Its x64 ABI is one pointer argument; forwarding it preserves
  // compatibility with overlays that import the public ordinal/name.
  using Fn = HRESULT(__stdcall*)(void*);
  auto fn = Proc<Fn>("DXGIReportAdapterConfiguration");
  return fn ? fn(configuration) : E_NOTIMPL;
}
HRESULT __stdcall DXGIDeclareAdapterRemovalSupport() {
  using Fn = HRESULT(__stdcall*)(); auto fn = Proc<Fn>("DXGIDeclareAdapterRemovalSupport");
  return fn ? fn() : E_NOTIMPL;
}
HRESULT __stdcall DXGIDisableVBlankVirtualization() {
  using Fn = HRESULT(__stdcall*)();
  auto fn = Proc<Fn>("DXGIDisableVBlankVirtualization");
  return fn ? fn() : E_NOTIMPL;
}
HRESULT __stdcall DXGID3D10CreateDevice(HMODULE a, void* b, void* c, UINT d, void* e, void** f) {
  using Fn = HRESULT(__stdcall*)(HMODULE, void*, void*, UINT, void*, void**);
  auto fn = Proc<Fn>("DXGID3D10CreateDevice"); return fn ? fn(a,b,c,d,e,f) : E_FAIL;
}
HRESULT __stdcall DXGID3D10CreateLayeredDevice(void* a, void* b, void* c, void* d, void* e) {
  using Fn = HRESULT(__stdcall*)(void*,void*,void*,void*,void*);
  auto fn = Proc<Fn>("DXGID3D10CreateLayeredDevice"); return fn ? fn(a,b,c,d,e) : E_FAIL;
}
SIZE_T __stdcall DXGID3D10GetLayeredDeviceSize(const void* a, UINT b) {
  using Fn = SIZE_T(__stdcall*)(const void*,UINT);
  auto fn = Proc<Fn>("DXGID3D10GetLayeredDeviceSize"); return fn ? fn(a,b) : 0;
}
HRESULT __stdcall DXGID3D10RegisterLayers(const void* a, UINT b) {
  using Fn = HRESULT(__stdcall*)(const void*,UINT);
  auto fn = Proc<Fn>("DXGID3D10RegisterLayers"); return fn ? fn(a,b) : E_FAIL;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    // The official loader only installs its very-early telemetry IAT hook here;
    // it does not load SKSE's core under the loader lock. The hook loads the
    // exact 1.5.97 core later at the same point as the normal SKSE Steam path.
    InitSKSESteamLoader();
  }
  return TRUE;
}
