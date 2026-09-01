#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstring>

#include "CombatLagFix.h"
#include "Log.h"
#include "Runtime.h"
#include "Version.h"

namespace {

HMODULE g_proxy_module = nullptr;
HMODULE g_real_module = nullptr;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
volatile LONG g_runtime_started = 0;

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID,
                                              LPVOID*, LPUNKNOWN);
using DllCanUnloadNowFn = HRESULT(WINAPI*)();
using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using DllRegistrationFn = HRESULT(WINAPI*)();

DirectInput8CreateFn g_direct_input_create = nullptr;
DllCanUnloadNowFn g_can_unload = nullptr;
DllGetClassObjectFn g_get_class_object = nullptr;
DllRegistrationFn g_register = nullptr;
DllRegistrationFn g_unregister = nullptr;

template <typename T>
T Resolve(const char* name) {
  const FARPROC address = g_real_module ? GetProcAddress(g_real_module, name) :
                                         nullptr;
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address),
                "Windows function pointers must have equal width");
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

bool IsI386Module(HMODULE module) {
  if (!module) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
         nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

UINT GetCompatibleSystemDirectory(wchar_t* output, UINT capacity) {
  if (!output || capacity < 2) return 0;
  // WEMU's ARM64EC prefix stores the host ARM64 system DLLs in system32 and
  // the guest PE32 DLLs in syswow64. Request that directory explicitly so a
  // 32-bit proxy never depends on Wine's filesystem-redirection behavior.
  const UINT wow64 = GetSystemWow64DirectoryW(output, capacity);
  if (wow64 && wow64 < capacity) return wow64;
  return GetSystemDirectoryW(output, capacity);
}

BOOL CALLBACK InitializeProxy(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t system[MAX_PATH] = {};
  const UINT length = GetCompatibleSystemDirectory(system, MAX_PATH);
  if (!length || length >= MAX_PATH - 13) return FALSE;
  lstrcatW(system, L"\\dinput8.dll");
  g_real_module = LoadLibraryW(system);
  if (!g_real_module || g_real_module == g_proxy_module) {
    if (g_real_module && g_real_module != g_proxy_module)
      FreeLibrary(g_real_module);
    g_real_module = nullptr;
    return FALSE;
  }
  if (!IsI386Module(g_real_module)) {
    FreeLibrary(g_real_module);
    g_real_module = nullptr;
    return FALSE;
  }
  g_direct_input_create = Resolve<DirectInput8CreateFn>("DirectInput8Create");
  g_can_unload = Resolve<DllCanUnloadNowFn>("DllCanUnloadNow");
  g_get_class_object = Resolve<DllGetClassObjectFn>("DllGetClassObject");
  g_register = Resolve<DllRegistrationFn>("DllRegisterServer");
  g_unregister = Resolve<DllRegistrationFn>("DllUnregisterServer");
  if (!g_direct_input_create) {
    FreeLibrary(g_real_module);
    g_real_module = nullptr;
    return FALSE;
  }
  fo3ds::InitializeLog(g_proxy_module);
  wchar_t loaded_path[MAX_PATH] = {};
  GetModuleFileNameW(g_real_module, loaded_path, MAX_PATH);
  fo3ds::Log(fo3ds::LogLevel::kInfo,
             "[PROXY] PE32 dinput8 ready requested=%ls loaded=%ls module=%p create=%u class=%u register=%u unregister=%u",
             system, loaded_path, g_real_module, g_direct_input_create ? 1u : 0u,
             g_get_class_object ? 1u : 0u, g_register ? 1u : 0u,
             g_unregister ? 1u : 0u);
  return TRUE;
}

bool EnsureInitialized() {
  return InitOnceExecuteOnce(&g_once, InitializeProxy, nullptr, nullptr) != FALSE &&
         g_direct_input_create != nullptr;
}

bool PinProxyForProcessLifetime() {
  HMODULE pinned = nullptr;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_PIN;
  if (!GetModuleHandleExW(flags,
                          reinterpret_cast<LPCWSTR>(&g_proxy_module),
                          &pinned) || pinned != g_proxy_module) {
    fo3ds::Log(fo3ds::LogLevel::kError,
               "[PROXY] failed to pin runtime module error=%lu; mod runtime disabled",
               static_cast<unsigned long>(GetLastError()));
    return false;
  }
  fo3ds::Log(fo3ds::LogLevel::kInfo,
             "[PROXY] runtime module pinned for process lifetime module=%p",
             pinned);
  return true;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
                                               REFIID iid, LPVOID* output,
                                               LPUNKNOWN outer) {
  if (output) *output = nullptr;
  if (!EnsureInitialized()) return DIERR_NOTFOUND;
  const HRESULT result =
      g_direct_input_create(instance, version, iid, output, outer);
  if (SUCCEEDED(result) && output && *output &&
      InterlockedCompareExchange(&g_runtime_started, -1, 0) == 0) {
    if (PinProxyForProcessLifetime()) {
      // Apply executable patches synchronously on Fallout's startup thread,
      // before DirectInput8Create returns.  This prevents a live game thread
      // from fetching a partially written rel32 call and also guarantees the
      // patched targets remain mapped for the process lifetime.
      const fo3ds::BuildIdentity build =
          fo3ds::IdentifyExecutable(GetModuleHandleW(nullptr));
      fo3ds::ApplyCombatLagFix(build);
      fo3ds::Log(fo3ds::LogLevel::kInfo,
                 "[PROXY] DirectInput8Create forwarded successfully; starting runtime");
      if (fo3ds::StartRuntime(g_proxy_module)) {
        InterlockedExchange(&g_runtime_started, 1);
      } else {
        fo3ds::Log(fo3ds::LogLevel::kError,
                   "[PROXY] runtime worker creation failed; a later DirectInput call may retry");
        InterlockedExchange(&g_runtime_started, 0);
      }
    } else {
      InterlockedExchange(&g_runtime_started, 0);
    }
  }
  return result;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
  if (!EnsureInitialized()) return S_FALSE;
  // The proxy owns process-lifetime panel/runtime workers. Never tell COM it
  // is safe to unmap this module while those entry points may still execute.
  if (InterlockedCompareExchange(&g_runtime_started, 0, 0) != 0)
    return S_FALSE;
  return g_can_unload ? g_can_unload() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid,
                                             LPVOID* output) {
  if (!EnsureInitialized()) return CLASS_E_CLASSNOTAVAILABLE;
  return g_get_class_object ? g_get_class_object(clsid, iid, output) :
                              CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
  if (!EnsureInitialized()) return E_NOTIMPL;
  return g_register ? g_register() : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
  if (!EnsureInitialized()) return E_NOTIMPL;
  return g_unregister ? g_unregister() : E_NOTIMPL;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_proxy_module = module;
    DisableThreadLibraryCalls(module);
  } else if (reason == DLL_PROCESS_DETACH) {
    fo3ds::NotifyProcessDetach();
  }
  return TRUE;
}
