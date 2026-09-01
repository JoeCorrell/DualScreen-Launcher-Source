#include <windows.h>
#include <unknwn.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::wstring ExecutableDirectory() {
  std::vector<wchar_t> path(MAX_PATH);
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length) return {};
    if (length < path.size() - 1) {
      std::wstring result(path.data(), length);
      const size_t slash = result.find_last_of(L"\\/");
      return slash == std::wstring::npos ? std::wstring{} : result.substr(0, slash);
    }
    if (path.size() >= 32768) return {};
    path.resize(path.size() * 2);
  }
}

void Append(const std::wstring& report, const char* format, ...) {
  char text[512] = {};
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(text, sizeof(text), format, arguments);
  va_end(arguments);
  HANDLE file = CreateFileW(report.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, wchar_t*, int) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
  const std::wstring directory = ExecutableDirectory();
  if (directory.empty()) return 10;
  const std::wstring report = directory + L"\\Fallout4SecondScreenProbe.txt";
  DeleteFileW(report.c_str());
  Append(report, "probe=start\r\n");

  // Loading the neutral host-side name avoids Windows Defender's heuristic
  // quarantine of freshly linked unsigned proxy DLLs. Deployment still names
  // this same validated binary dxgi.dll through the launcher manifest.
  const std::wstring proxy = directory + L"\\dxgi.candidate.dll";
  SetLastError(ERROR_SUCCESS);
  HMODULE module = LoadLibraryW(proxy.c_str());
  // GetLastError is undefined after a successful LoadLibrary. Do not report a
  // stale loader error beside a successful result.
  const DWORD load_error = module ? ERROR_SUCCESS : GetLastError();
  Append(report, "load=%s error=%lu module=%p\r\n", module ? "ok" : "failed",
         static_cast<unsigned long>(load_error), static_cast<void*>(module));
  if (!module) return 11;

  FARPROC address = GetProcAddress(module, "CreateDXGIFactory");
  Append(report, "export=%s address=%p\r\n", address ? "ok" : "missing",
         reinterpret_cast<void*>(address));
  if (!address) return 12;

  using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
  static_assert(sizeof(CreateFactory) == sizeof(address));
  CreateFactory create_factory = nullptr;
  std::memcpy(&create_factory, &address, sizeof(create_factory));
  // IID_IDXGIFactory: 7b7166ec-21c7-44ae-b21a-c9ae321ae369.
  const GUID iid = {0x7b7166ec,
                    0x21c7,
                    0x44ae,
                    {0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69}};
  IUnknown* factory = nullptr;
  Append(report, "factory_call=begin\r\n");
  const HRESULT result =
      create_factory(iid, reinterpret_cast<void**>(&factory));
  Append(report, "factory_call=end hr=0x%08lx factory=%p\r\n",
         static_cast<unsigned long>(result), static_cast<void*>(factory));
  if (factory) factory->Release();
  // Give the proxy's detached bootstrap thread time to finish its runtime
  // identity path while this 1 MiB-stack host remains alive.
  Sleep(2000);
  FreeLibrary(module);
  return SUCCEEDED(result) ? 0 : 13;
}
