#include <windows.h>
#include <dxgi.h>

#include <cstring>

#include "Mod.h"

namespace {

HMODULE g_proxy = nullptr;
HMODULE g_real = nullptr;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

using FactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using Factory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

template <typename T>
T Proc(const char* name) {
  FARPROC address = g_real ? GetProcAddress(g_real, name) : nullptr;
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address),
                "function pointer representation mismatch");
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

bool IsI386(HMODULE module) {
  if (!module) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
         nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

BOOL CALLBACK InitializeReal(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH] = {};
  UINT length = GetSystemWow64DirectoryW(path, MAX_PATH);
  if (!length || length >= MAX_PATH - 10)
    length = GetSystemDirectoryW(path, MAX_PATH);
  if (!length || length >= MAX_PATH - 10) return FALSE;
  lstrcatW(path, L"\\dxgi.dll");
  g_real = LoadLibraryW(path);
  if (!g_real || g_real == g_proxy || !IsI386(g_real)) {
    if (g_real && g_real != g_proxy) FreeLibrary(g_real);
    g_real = nullptr;
    return FALSE;
  }
  return Proc<FactoryFn>("CreateDXGIFactory1") != nullptr;
}

bool EnsureReal() {
  return InitOnceExecuteOnce(&g_once, InitializeReal, nullptr, nullptr) != FALSE &&
         g_real;
}

void StartBeforeFactory() {
  HMODULE pinned = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_PIN,
                        reinterpret_cast<LPCWSTR>(&g_proxy), &pinned) &&
      pinned == g_proxy)
    gdds::StartMod(g_proxy);
}

}  // namespace

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID iid, void** output) {
  if (output) *output = nullptr;
  if (!EnsureReal()) return E_FAIL;
  FactoryFn function = Proc<FactoryFn>("CreateDXGIFactory");
  StartBeforeFactory();
  return function ? function(iid, output) : E_NOINTERFACE;
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void** output) {
  if (output) *output = nullptr;
  if (!EnsureReal()) return E_FAIL;
  FactoryFn function = Proc<FactoryFn>("CreateDXGIFactory1");
  StartBeforeFactory();
  return function ? function(iid, output) : E_NOINTERFACE;
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid,
                                               void** output) {
  if (output) *output = nullptr;
  if (!EnsureReal()) return E_FAIL;
  Factory2Fn function = Proc<Factory2Fn>("CreateDXGIFactory2");
  StartBeforeFactory();
  HRESULT result = E_NOINTERFACE;
  if (function) {
    result = function(flags, iid, output);
  } else {
    FactoryFn fallback = Proc<FactoryFn>("CreateDXGIFactory1");
    if (fallback) result = fallback(iid, output);
  }
  return result;
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid,
                                                  void** output) {
  using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
  if (!EnsureReal()) return E_NOINTERFACE;
  Fn function = Proc<Fn>("DXGIGetDebugInterface1");
  return function ? function(flags, iid, output) : E_NOINTERFACE;
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
  using Fn = HRESULT(WINAPI*)();
  if (!EnsureReal()) return E_NOTIMPL;
  Fn function = Proc<Fn>("DXGIDeclareAdapterRemovalSupport");
  return function ? function() : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DXGID3D10CreateDevice(HMODULE module, void* factory,
                                                  void* adapter, UINT flags,
                                                  void* unknown, void** device) {
  using Fn = HRESULT(WINAPI*)(HMODULE, void*, void*, UINT, void*, void**);
  if (!EnsureReal()) return E_FAIL;
  Fn function = Proc<Fn>("DXGID3D10CreateDevice");
  return function ? function(module, factory, adapter, flags, unknown, device)
                  : E_FAIL;
}

extern "C" HRESULT WINAPI DXGID3D10CreateLayeredDevice(
    void* a, void* b, void* c, void* d, void* e) {
  using Fn = HRESULT(WINAPI*)(void*, void*, void*, void*, void*);
  if (!EnsureReal()) return E_FAIL;
  Fn function = Proc<Fn>("DXGID3D10CreateLayeredDevice");
  return function ? function(a, b, c, d, e) : E_FAIL;
}

extern "C" SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(const void* layers,
                                                         UINT count) {
  using Fn = SIZE_T(WINAPI*)(const void*, UINT);
  if (!EnsureReal()) return 0;
  Fn function = Proc<Fn>("DXGID3D10GetLayeredDeviceSize");
  return function ? function(layers, count) : 0;
}

extern "C" HRESULT WINAPI DXGID3D10RegisterLayers(const void* layers,
                                                    UINT count) {
  using Fn = HRESULT(WINAPI*)(const void*, UINT);
  if (!EnsureReal()) return E_FAIL;
  Fn function = Proc<Fn>("DXGID3D10RegisterLayers");
  return function ? function(layers, count) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_proxy = module;
    DisableThreadLibraryCalls(module);
  }
  return TRUE;
}
