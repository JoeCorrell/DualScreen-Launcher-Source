#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace fomds {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;

struct RateEntry {
  char key[64] = {};
  ULONGLONG last_ms = 0;
};

RateEntry g_rates[48] = {};

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarning: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "UNKNOWN";
}

void WriteV(LogLevel level, const char* format, va_list arguments) {
  if (g_file == INVALID_HANDLE_VALUE || !format) return;
  char message[2048] = {};
  std::vsnprintf(message, sizeof(message), format, arguments);
  SYSTEMTIME time = {};
  GetLocalTime(&time);
  char line[2304] = {};
  const int length = std::snprintf(
      line, sizeof(line),
      "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
      static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
      static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
      static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
      static_cast<unsigned>(time.wMilliseconds), LevelName(level), message);
  if (length <= 0) return;
  const DWORD bytes = static_cast<DWORD>(
      length < static_cast<int>(sizeof(line)) ? length : sizeof(line) - 1);
  DWORD written = 0;
  WriteFile(g_file, line, bytes, &written, nullptr);
  if (level == LogLevel::kError) FlushFileBuffers(g_file);
}

}  // namespace

bool InitializeNamedLog(HMODULE module, const wchar_t* file_name,
                        const wchar_t* previous_file_name) {
  if (!file_name || !*file_name || !previous_file_name ||
      !*previous_file_name) {
    return false;
  }
  std::vector<wchar_t> module_path(32768);
  const DWORD length = GetModuleFileNameW(
      module, module_path.data(), static_cast<DWORD>(module_path.size()));
  if (!length || length >= module_path.size()) return false;
  std::wstring directory(module_path.data(), length);
  const std::size_t slash = directory.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  directory.resize(slash + 1);
  const std::wstring current = directory + file_name;
  const std::wstring previous = directory + previous_file_name;
  MoveFileExW(current.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
  g_file = CreateFileW(current.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) return false;
  Log(LogLevel::kInfo, "[INIT] logging initialized pid=%lu",
      static_cast<unsigned long>(GetCurrentProcessId()));
  return true;
}

bool InitializeLog(HMODULE module) {
  return InitializeNamedLog(module, L"FieldsOfMistriaSecondScreen.log",
                            L"FieldsOfMistriaSecondScreen.previous.log");
}

void Log(LogLevel level, const char* format, ...) {
  AcquireSRWLockExclusive(&g_lock);
  va_list arguments;
  va_start(arguments, format);
  WriteV(level, format, arguments);
  va_end(arguments);
  ReleaseSRWLockExclusive(&g_lock);
}

void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...) {
  if (!key || !*key) return;
  const ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  RateEntry* selected = nullptr;
  for (RateEntry& entry : g_rates) {
    if (entry.key[0] && std::strcmp(entry.key, key) == 0) {
      selected = &entry;
      break;
    }
    if (!entry.key[0] && !selected) selected = &entry;
  }
  if (selected && selected->key[0] && now - selected->last_ms < interval_ms) {
    ReleaseSRWLockExclusive(&g_lock);
    return;
  }
  if (selected) {
    std::snprintf(selected->key, sizeof(selected->key), "%s", key);
    selected->last_ms = now;
  }
  va_list arguments;
  va_start(arguments, format);
  WriteV(level, format, arguments);
  va_end(arguments);
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace fomds
