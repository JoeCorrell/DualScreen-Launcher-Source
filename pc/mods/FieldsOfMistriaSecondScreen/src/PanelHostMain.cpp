#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#include "BuildIdentity.h"
#include "Config.h"
#include "ExpectedBuild.h"
#include "Log.h"
#include "Panel.h"
#include "StateStore.h"

namespace {

bool ModuleDirectory(HMODULE module, std::wstring* directory) {
  if (!directory) return false;
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(
      module, path.data(), static_cast<DWORD>(path.size()));
  if (!length || length >= path.size()) return false;
  directory->assign(path.data(), length);
  const std::size_t slash = directory->find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  directory->resize(slash + 1);
  return true;
}

DWORD ParseParentPid(int argc, wchar_t** argv) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (_wcsicmp(argv[index], L"--parent-pid") != 0) continue;
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(argv[index + 1], &end, 10);
    if (end && *end == L'\0' && value > 0) {
      return static_cast<DWORD>(value);
    }
  }
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  fomds::InitializeNamedLog(
      instance, L"FieldsOfMistriaSecondScreenHost.log",
      L"FieldsOfMistriaSecondScreenHost.previous.log");
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const DWORD parent_pid = argv ? ParseParentPid(argc, argv) : 0;
  if (argv) LocalFree(argv);
  if (!parent_pid || parent_pid == GetCurrentProcessId()) {
    fomds::Log(fomds::LogLevel::kError,
               "[HOST] rejected missing or invalid parent pid");
    return 2;
  }

  const std::wstring mutex_name =
      L"Local\\FieldsOfMistriaSecondScreenHost-" +
      std::to_wstring(parent_pid);
  HANDLE singleton = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
  if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
    fomds::Log(fomds::LogLevel::kWarning,
               "[HOST] duplicate host refused parent=%lu",
               static_cast<unsigned long>(parent_pid));
    if (singleton) CloseHandle(singleton);
    return 3;
  }
  // Wine/FEX only needs to grant the wait right. Query permission is not used
  // and can make an otherwise valid parent handle fail under compatibility
  // layers with narrower process-access emulation.
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
  if (!parent) {
    fomds::Log(fomds::LogLevel::kError,
               "[HOST] could not open parent=%lu error=%lu",
               static_cast<unsigned long>(parent_pid),
               static_cast<unsigned long>(GetLastError()));
    CloseHandle(singleton);
    return 4;
  }

  std::wstring directory;
  if (!ModuleDirectory(instance, &directory)) {
    fomds::Log(fomds::LogLevel::kError,
               "[HOST] could not resolve adjacent game paths");
    CloseHandle(parent);
    CloseHandle(singleton);
    return 5;
  }
  const std::wstring executable = directory + L"FieldsOfMistria.exe";
  const std::wstring original_sdl = directory + fomds::kOriginalSdlName;
  const fomds::BuildIdentity identity = fomds::IdentifyBuildFromPaths(
      executable.c_str(), original_sdl.c_str());
  fomds::Log(identity.supported ? fomds::LogLevel::kInfo
                                : fomds::LogLevel::kWarning,
             "[COMPAT] %s", identity.reason);
  fomds::ResetState();
  fomds::PublishCompatibility(
      identity, identity.supported ? fomds::Lifecycle::kCompatible
                                   : fomds::Lifecycle::kUnsupported);
  if (!identity.supported) {
    fomds::Log(fomds::LogLevel::kWarning,
               "[HOST] exact build gate failed; panel remains disabled");
    CloseHandle(parent);
    CloseHandle(singleton);
    return 6;
  }

  const fomds::Config config = fomds::LoadConfig();
  if (!fomds::StartPanel(instance, config, identity)) {
    fomds::Log(fomds::LogLevel::kError,
               "[HOST] panel startup failed without affecting the game");
    CloseHandle(parent);
    CloseHandle(singleton);
    return 7;
  }
  fomds::Log(fomds::LogLevel::kInfo,
             "[HOST] isolated panel active parent=%lu",
             static_cast<unsigned long>(parent_pid));

  WaitForSingleObject(parent, INFINITE);
  fomds::Log(fomds::LogLevel::kInfo,
             "[HOST] parent exited; stopping isolated panel");
  fomds::StopPanel(5000);
  CloseHandle(parent);
  CloseHandle(singleton);
  return 0;
}
