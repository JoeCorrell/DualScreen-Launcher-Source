#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace wfcds {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
wchar_t g_path[MAX_PATH] = {};

struct RateEntry { char key[48]; DWORD tick; };
RateEntry g_rates[32] = {};

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarning: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "INFO";
}

void Write(LogLevel level, const char* format, va_list arguments) {
  char message[2048] = {};
  std::vsnprintf(message, sizeof(message), format, arguments);
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  char line[2304] = {};
  std::snprintf(line, sizeof(line),
                "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, LevelName(level), message);
  OutputDebugStringA(line);
  AcquireSRWLockExclusive(&g_lock);
  HANDLE file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ |
                            FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written,
              nullptr);
    CloseHandle(file);
  }
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace

void InitializeLog(HMODULE module) {
  wchar_t directory[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, directory, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(directory, L'\\');
  if (!slash) return;
  *(slash + 1) = L'\0';
  constexpr wchar_t kLogName[] = L"TransformersWfcSecondScreen.log";
  const size_t prefix = static_cast<size_t>(slash + 1 - directory);
  if (prefix + (sizeof(kLogName) / sizeof(kLogName[0])) > MAX_PATH) return;
  std::memcpy(g_path, directory, prefix * sizeof(wchar_t));
  std::memcpy(g_path + prefix, kLogName, sizeof(kLogName));
}

void Log(LogLevel level, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Write(level, format, arguments);
  va_end(arguments);
}

void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...) {
  if (!key || !*key) return;
  const DWORD now = GetTickCount();
  bool allowed = false;
  AcquireSRWLockExclusive(&g_lock);
  RateEntry* empty = nullptr;
  for (RateEntry& entry : g_rates) {
    if (!entry.key[0] && !empty) empty = &entry;
    if (std::strcmp(entry.key, key) != 0) continue;
    if (now - entry.tick >= interval_ms) {
      entry.tick = now;
      allowed = true;
    }
    empty = nullptr;
    break;
  }
  if (empty) {
    std::snprintf(empty->key, sizeof(empty->key), "%s", key);
    empty->tick = now;
    allowed = true;
  }
  ReleaseSRWLockExclusive(&g_lock);
  if (!allowed) return;
  va_list arguments;
  va_start(arguments, format);
  Write(level, format, arguments);
  va_end(arguments);
}

}  // namespace wfcds
