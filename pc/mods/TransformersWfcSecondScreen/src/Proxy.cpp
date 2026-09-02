#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdint>
#include <cstring>

#include "Log.h"
#include "Mod.h"

namespace {

HMODULE g_proxy = nullptr;
HMODULE g_real = nullptr;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
volatile LONG g_started = 0;

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID,
                                              LPVOID*, LPUNKNOWN);
using DllCanUnloadNowFn = HRESULT(WINAPI*)();
using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using RegistrationFn = HRESULT(WINAPI*)();

DirectInput8CreateFn g_create = nullptr;
DllCanUnloadNowFn g_can_unload = nullptr;
DllGetClassObjectFn g_get_class = nullptr;
RegistrationFn g_register = nullptr;
RegistrationFn g_unregister = nullptr;

template <typename T>
T Resolve(const char* name) {
  FARPROC address = g_real ? GetProcAddress(g_real, name) : nullptr;
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address), "function pointer mismatch");
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

bool IsI386(HMODULE module) {
  if (!module) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
         nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t directory[MAX_PATH] = {};
  UINT length = GetSystemWow64DirectoryW(directory, MAX_PATH);
  if (!length || length >= MAX_PATH - 13)
    length = GetSystemDirectoryW(directory, MAX_PATH);
  if (!length || length >= MAX_PATH - 13) return FALSE;
  lstrcatW(directory, L"\\dinput8.dll");
  g_real = LoadLibraryW(directory);
  if (!g_real || g_real == g_proxy || !IsI386(g_real)) {
    if (g_real && g_real != g_proxy) FreeLibrary(g_real);
    g_real = nullptr;
    return FALSE;
  }
  g_create = Resolve<DirectInput8CreateFn>("DirectInput8Create");
  g_can_unload = Resolve<DllCanUnloadNowFn>("DllCanUnloadNow");
  g_get_class = Resolve<DllGetClassObjectFn>("DllGetClassObject");
  g_register = Resolve<RegistrationFn>("DllRegisterServer");
  g_unregister = Resolve<RegistrationFn>("DllUnregisterServer");
  wfcds::InitializeLog(g_proxy);
  return g_create != nullptr;
}

bool Ensure() {
  return InitOnceExecuteOnce(&g_once, Initialize, nullptr, nullptr) != FALSE &&
         g_create;
}

}  // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
                                               REFIID iid, LPVOID* output,
                                               LPUNKNOWN outer) {
  if (output) *output = nullptr;
  if (!Ensure()) return DIERR_NOTFOUND;
  const HRESULT result = g_create(instance, version, iid, output, outer);
  if (SUCCEEDED(result) && output && *output &&
      InterlockedCompareExchange(&g_started, 1, 0) == 0) {
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&g_proxy), &pinned) &&
        pinned == g_proxy && wfcds::StartMod(g_proxy)) {
      wfcds::Log(wfcds::LogLevel::kInfo,
                 "[DualScreen] dinput8 forwarded; runtime started");
    } else {
      InterlockedExchange(&g_started, 0);
    }
  }
  return result;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
  if (!Ensure()) return S_FALSE;
  return InterlockedCompareExchange(&g_started, 0, 0) != 0
             ? S_FALSE
             : (g_can_unload ? g_can_unload() : S_FALSE);
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid,
                                             LPVOID* output) {
  if (!Ensure()) return CLASS_E_CLASSNOTAVAILABLE;
  return g_get_class ? g_get_class(clsid, iid, output) :
                       CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
  return Ensure() && g_register ? g_register() : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
  return Ensure() && g_unregister ? g_unregister() : E_NOTIMPL;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_proxy = module;
    DisableThreadLibraryCalls(module);
  } else if (reason == DLL_PROCESS_DETACH) {
    wfcds::NotifyProcessDetach();
  }
  return TRUE;
}
