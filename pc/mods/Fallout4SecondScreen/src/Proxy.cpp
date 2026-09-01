#include <windows.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "Log.h"
#include "Mod.h"
#include "ProxyExports.h"

namespace {

HMODULE g_real = nullptr;
HMODULE g_self = nullptr;
INIT_ONCE g_real_once = INIT_ONCE_STATIC_INIT;
DWORD g_load_error = ERROR_SUCCESS;
volatile LONG g_error_logged = 0;
volatile LONG g_success_logged = 0;
bool g_same_module = false;

BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const UINT count =
        GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (!count) {
      g_load_error = GetLastError();
      break;
    }
    if (count >= buffer.size()) {
      if (count >= 32768) {
        g_load_error = ERROR_BUFFER_OVERFLOW;
        break;
      }
      buffer.resize(static_cast<size_t>(count) + 1);
      continue;
    }
    std::wstring path(buffer.data(), count);
    path += L"\\dxgi.dll";
    g_real = LoadLibraryW(path.c_str());
    if (!g_real) {
      g_load_error = GetLastError();
    } else if (g_real == g_self) {
      // Defensive guard for container-specific path redirection or file
      // identity aliasing. Upstream Wine normally keeps distinct absolute
      // module paths separate.
      g_same_module = true;
      FreeLibrary(g_real);
      g_real = nullptr;
      g_load_error = ERROR_ALREADY_EXISTS;
    }
    break;
  }
  return g_real != nullptr;
}

FARPROC Resolve(const char* name) {
  const BOOL loaded = InitOnceExecuteOnce(&g_real_once, LoadReal, nullptr, nullptr);
  if ((!loaded || !g_real) &&
      InterlockedCompareExchange(&g_error_logged, 1, 0) == 0) {
    fo4ds::InitializeLog();
    fo4ds::Log(fo4ds::LogLevel::kError,
               "[PROXY] system DXGI load failed error=%lu same_module=%u",
               static_cast<unsigned long>(g_load_error),
               g_same_module ? 1u : 0u);
    char message[192] = {};
    std::snprintf(message, sizeof(message),
                  "Fallout4SecondScreen: system32\\dxgi.dll load failed error=%lu\n",
                  static_cast<unsigned long>(g_load_error));
    OutputDebugStringA(message);
  }
  if (loaded && g_real &&
      InterlockedCompareExchange(&g_success_logged, 1, 0) == 0) {
    fo4ds::InitializeLog();
    fo4ds::Log(fo4ds::LogLevel::kInfo,
               "[PROXY] system DXGI loaded real=%p self=%p",
               static_cast<void*>(g_real), static_cast<void*>(g_self));
  }
  return g_real ? GetProcAddress(g_real, name) : nullptr;
}

template <typename Function>
Function Proc(const char* name) {
  static_assert(sizeof(Function) == sizeof(FARPROC));
  FARPROC address = Resolve(name);
  Function function = nullptr;
  std::memcpy(&function, &address, sizeof(function));
  return function;
}

}  // namespace

extern "C" {

HRESULT WINAPI CreateDXGIFactory(REFIID iid, void** output) {
  auto function = Proc<fo4ds::proxy_abi::CreateDXGIFactoryFn>(
      "CreateDXGIFactory");
  if (!function) return E_FAIL;
  fo4ds::LogRateLimited("proxy_factory_enter", 60000, fo4ds::LogLevel::kInfo,
                        "[PROXY] CreateDXGIFactory enter");
  const HRESULT result = function(iid, output);
  fo4ds::LogRateLimited("proxy_factory_exit", 60000, fo4ds::LogLevel::kInfo,
                        "[PROXY] CreateDXGIFactory exit hr=0x%08lx output=%p",
                        static_cast<unsigned long>(result),
                        output ? *output : nullptr);
  if (SUCCEEDED(result)) fo4ds::ModOnDxgiUsed();
  return result;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void** output) {
  auto function = Proc<fo4ds::proxy_abi::CreateDXGIFactoryFn>(
      "CreateDXGIFactory1");
  if (!function) return E_FAIL;
  const HRESULT result = function(iid, output);
  if (SUCCEEDED(result)) fo4ds::ModOnDxgiUsed();
  return result;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid, void** output) {
  auto function = Proc<fo4ds::proxy_abi::CreateDXGIFactory2Fn>(
      "CreateDXGIFactory2");
  if (function) {
    const HRESULT result = function(flags, iid, output);
    if (SUCCEEDED(result)) fo4ds::ModOnDxgiUsed();
    return result;
  }
  return CreateDXGIFactory1(iid, output);
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid, void** output) {
  auto function = Proc<fo4ds::proxy_abi::DXGIGetDebugInterface1Fn>(
      "DXGIGetDebugInterface1");
  return function ? function(flags, iid, output) : E_NOINTERFACE;
}

HRESULT WINAPI DXGIReportAdapterConfiguration(void* configuration) {
  auto function =
      Proc<fo4ds::proxy_abi::DXGIReportAdapterConfigurationFn>(
          "DXGIReportAdapterConfiguration");
  return function ? function(configuration) : E_NOTIMPL;
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
  auto function =
      Proc<fo4ds::proxy_abi::DXGIDeclareAdapterRemovalSupportFn>(
          "DXGIDeclareAdapterRemovalSupport");
  return function ? function() : E_NOTIMPL;
}

HRESULT WINAPI DXGIDisableVBlankVirtualization() {
  auto function =
      Proc<fo4ds::proxy_abi::DXGIDisableVBlankVirtualizationFn>(
          "DXGIDisableVBlankVirtualization");
  return function ? function() : E_NOTIMPL;
}

HRESULT WINAPI DXGID3D10CreateDevice(HMODULE module, void* factory,
                                     void* adapter, UINT flags,
                                     void* unknown, void** device) {
  auto function = Proc<fo4ds::proxy_abi::DXGID3D10CreateDeviceFn>(
      "DXGID3D10CreateDevice");
  return function
             ? function(module, factory, adapter, flags, unknown, device)
             : E_FAIL;
}

HRESULT WINAPI DXGID3D10CreateLayeredDevice(void* a, void* b, void* c,
                                            void* d, void* e) {
  auto function = Proc<fo4ds::proxy_abi::DXGID3D10CreateLayeredDeviceFn>(
      "DXGID3D10CreateLayeredDevice");
  return function ? function(a, b, c, d, e) : E_FAIL;
}

SIZE_T WINAPI DXGID3D10GetLayeredDeviceSize(const void* a, UINT b) {
  auto function = Proc<fo4ds::proxy_abi::DXGID3D10GetLayeredDeviceSizeFn>(
      "DXGID3D10GetLayeredDeviceSize");
  return function ? function(a, b) : 0;
}

HRESULT WINAPI DXGID3D10RegisterLayers(const void* a, UINT b) {
  auto function = Proc<fo4ds::proxy_abi::DXGID3D10RegisterLayersFn>(
      "DXGID3D10RegisterLayers");
  return function ? function(a, b) : E_FAIL;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = instance;
  }
  return TRUE;
}
