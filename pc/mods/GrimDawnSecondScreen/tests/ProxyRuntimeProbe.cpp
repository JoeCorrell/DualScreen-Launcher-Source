#include <windows.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>

int wmain(int count, wchar_t** arguments) {
  if (count != 2) {
    std::fprintf(stderr, "usage: ProxyRuntimeProbe <dxgi.dll>\n");
    return 2;
  }
  HMODULE proxy = LoadLibraryW(arguments[1]);
  if (!proxy) {
    std::fprintf(stderr, "FAIL: LoadLibrary error=%lu\n",
                 static_cast<unsigned long>(GetLastError()));
    return 1;
  }
  FARPROC raw = GetProcAddress(proxy, "CreateDXGIFactory1");
  using FactoryFn = HRESULT(WINAPI*)(REFIID, void**);
  FactoryFn create_factory = nullptr;
  static_assert(sizeof(raw) == sizeof(create_factory),
                "function pointer representation mismatch");
  std::memcpy(&create_factory, &raw, sizeof(create_factory));
  IDXGIFactory1* factory = nullptr;
  const HRESULT result = create_factory
                             ? create_factory(__uuidof(IDXGIFactory1),
                                              reinterpret_cast<void**>(&factory))
                             : E_NOINTERFACE;
  if (factory) factory->Release();
  if (FAILED(result)) {
    std::fprintf(stderr, "FAIL: forwarded CreateDXGIFactory1 hr=%08lx\n",
                 static_cast<unsigned long>(result));
    return 1;
  }
  // The proxy's worker hashes this unrelated host and must fail closed. Allow
  // it to finish so unload races cannot hide in a successful factory call.
  Sleep(750);
  FreeLibrary(proxy);
  std::puts("ProxyRuntimeProbe passed");
  return 0;
}
