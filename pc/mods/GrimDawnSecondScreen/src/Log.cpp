#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstring>

namespace gdds {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;

struct RateEntry {
  const char* key = nullptr;
  ULONGLONG last = 0;
};

RateEntry g_rates[32] = {};

const char* Name(LogLevel level) {
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
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  char line[2304] = {};
  const int length = std::snprintf(
      line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
      static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
      static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
      static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
      static_cast<unsigned>(now.wMilliseconds), Name(level), message);
  if (length <= 0) return;
  const DWORD bytes = static_cast<DWORD>(
      length < static_cast<int>(sizeof(line)) ? length : sizeof(line) - 1);
  DWORD written = 0;
  WriteFile(g_file, line, bytes, &written, nullptr);
  if (level == LogLevel::kError) FlushFileBuffers(g_file);
}

}  // namespace

bool InitializeLog(HMODULE module) {
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return false;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return false;
  slash[1] = L'\0';
  wchar_t previous[MAX_PATH] = {};
  wchar_t current[MAX_PATH] = {};
  std::swprintf(previous, MAX_PATH, L"%lsGrimDawnSecondScreen.previous.log",
                path);
  std::swprintf(current, MAX_PATH, L"%lsGrimDawnSecondScreen.log", path);
  MoveFileExW(current, previous, MOVEFILE_REPLACE_EXISTING);
  g_file = CreateFileW(current, GENERIC_WRITE, FILE_SHARE_READ,
                       nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) return false;
  Log(LogLevel::kInfo, "[INIT] logging initialized pid=%lu",
      static_cast<unsigned long>(GetCurrentProcessId()));
  return true;
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
  if (!key) return;
  const ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&g_lock);
  RateEntry* selected = nullptr;
  for (RateEntry& entry : g_rates) {
    if (entry.key && std::strcmp(entry.key, key) == 0) {
      selected = &entry;
      break;
    }
    if (!entry.key && !selected) selected = &entry;
  }
  if (selected && selected->key && now - selected->last < interval_ms) {
    ReleaseSRWLockExclusive(&g_lock);
    return;
  }
  if (selected) {
    selected->key = key;
    selected->last = now;
  }
  va_list arguments;
  va_start(arguments, format);
  WriteV(level, format, arguments);
  va_end(arguments);
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace gdds
