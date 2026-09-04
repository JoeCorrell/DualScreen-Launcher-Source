#include "Log.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace isaacds {
namespace {

SRWLOCK g_rate_lock = SRWLOCK_INIT;
SRWLOCK g_queue_lock = SRWLOCK_INIT;
wchar_t g_path[MAX_PATH] = {};
HANDLE g_event = nullptr;
HANDLE g_thread = nullptr;
volatile LONG g_stopping = 0;

struct RateEntry {
  char key[48];
  DWORD tick;
};
RateEntry g_rates[32] = {};

constexpr uint32_t kQueueCapacity = 128;
struct LogRecord {
  char line[2304];
};
LogRecord g_queue[kQueueCapacity] = {};
uint32_t g_queue_read = 0;
uint32_t g_queue_write = 0;
uint32_t g_queue_count = 0;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarning:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "INFO";
}

void Enqueue(LogLevel level, const char* format, va_list arguments) {
  char message[2048] = {};
  std::vsnprintf(message, sizeof(message), format, arguments);
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  char line[2304] = {};
  std::snprintf(line, sizeof(line),
                "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds, LevelName(level), message);
  AcquireSRWLockExclusive(&g_queue_lock);
  if (g_queue_count == kQueueCapacity) {
    g_queue_read = (g_queue_read + 1) % kQueueCapacity;
    --g_queue_count;
  }
  std::snprintf(g_queue[g_queue_write].line,
                sizeof(g_queue[g_queue_write].line), "%s", line);
  g_queue_write = (g_queue_write + 1) % kQueueCapacity;
  ++g_queue_count;
  ReleaseSRWLockExclusive(&g_queue_lock);
  if (g_event) SetEvent(g_event);
}

bool Pop(LogRecord* output) {
  if (!output) return false;
  AcquireSRWLockExclusive(&g_queue_lock);
  const bool available = g_queue_count != 0;
  if (available) {
    *output = g_queue[g_queue_read];
    g_queue[g_queue_read] = {};
    g_queue_read = (g_queue_read + 1) % kQueueCapacity;
    --g_queue_count;
  }
  ReleaseSRWLockExclusive(&g_queue_lock);
  return available;
}

bool QueueEmpty() {
  AcquireSRWLockShared(&g_queue_lock);
  const bool empty = g_queue_count == 0;
  ReleaseSRWLockShared(&g_queue_lock);
  return empty;
}

DWORD WINAPI LogWorker(void*) {
  for (;;) {
    if (g_event) WaitForSingleObject(g_event, INFINITE);
    if (QueueEmpty()) {
      if (InterlockedCompareExchange(&g_stopping, 0, 0) != 0) break;
      continue;
    }
    HANDLE file = CreateFileW(g_path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LogRecord record = {};
    while (Pop(&record)) {
      OutputDebugStringA(record.line);
      if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, record.line,
                  static_cast<DWORD>(std::strlen(record.line)), &written,
                  nullptr);
      }
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (InterlockedCompareExchange(&g_stopping, 0, 0) != 0 && QueueEmpty())
      break;
  }
  return 0;
}

}  // namespace

void InitializeLog(HMODULE module) {
  if (g_thread || !module) return;
  wchar_t directory[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, directory, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(directory, L'\\');
  if (!slash) return;
  *(slash + 1) = L'\0';
  constexpr wchar_t kName[] = L"IsaacSecondScreen.log";
  const size_t prefix = static_cast<size_t>(slash + 1 - directory);
  if (prefix + (sizeof(kName) / sizeof(kName[0])) > MAX_PATH) return;
  std::memcpy(g_path, directory, prefix * sizeof(wchar_t));
  std::memcpy(g_path + prefix, kName, sizeof(kName));

  InterlockedExchange(&g_stopping, 0);
  g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_event) return;
  g_thread = CreateThread(nullptr, 0, LogWorker, nullptr, 0, nullptr);
  if (!g_thread) {
    CloseHandle(g_event);
    g_event = nullptr;
  } else {
    SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
  }
}

void ShutdownLog() {
  InterlockedExchange(&g_stopping, 1);
  if (g_event) SetEvent(g_event);
  if (g_thread) {
    if (WaitForSingleObject(g_thread, 2000) != WAIT_OBJECT_0) return;
    CloseHandle(g_thread);
  }
  if (g_event) CloseHandle(g_event);
  g_thread = nullptr;
  g_event = nullptr;
}

void Log(LogLevel level, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  Enqueue(level, format, arguments);
  va_end(arguments);
}

void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...) {
  if (!key || !*key) return;
  const DWORD now = GetTickCount();
  bool allowed = false;
  AcquireSRWLockExclusive(&g_rate_lock);
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
  ReleaseSRWLockExclusive(&g_rate_lock);
  if (!allowed) return;
  va_list arguments;
  va_start(arguments, format);
  Enqueue(level, format, arguments);
  va_end(arguments);
}

}  // namespace isaacds
