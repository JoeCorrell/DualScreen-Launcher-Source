#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fo3ds {
namespace {

SRWLOCK g_log_lock = SRWLOCK_INIT;
FILE* g_log = nullptr;

struct RateEntry {
  uint32_t hash = 0;
  uint64_t last_ms = 0;
  uint32_t suppressed = 0;
};

constexpr uint32_t kRateCapacity = 64;
constexpr uint64_t kMaximumLogBytes = 2ull * 1024ull * 1024ull;
RateEntry g_rates[kRateCapacity] = {};

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarning: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "UNKNOWN";
}

uint32_t HashKey(const char* key) {
  uint32_t hash = 2166136261u;
  for (const unsigned char* at =
           reinterpret_cast<const unsigned char*>(key ? key : "");
       *at; ++at) {
    hash ^= *at;
    hash *= 16777619u;
  }
  return hash ? hash : 1u;
}

void WriteLocked(LogLevel level, const char* message, uint32_t suppressed) {
  if (!g_log) return;
  SYSTEMTIME time = {};
  GetLocalTime(&time);
  std::fprintf(g_log,
               "%04u-%02u-%02u %02u:%02u:%02u.%03u [tid=%lu] [%s] %s",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, time.wMilliseconds, GetCurrentThreadId(),
               LevelName(level), message ? message : "");
  if (suppressed)
    std::fprintf(g_log, " (suppressed=%u)", suppressed);
  std::fputc('\n', g_log);
  std::fflush(g_log);
}

void Format(char* output, size_t capacity, const char* format, va_list args) {
  if (!output || !capacity) return;
  std::vsnprintf(output, capacity, format ? format : "", args);
  output[capacity - 1] = '\0';
}

}  // namespace

void InitializeLog(void* raw_module) {
  HMODULE module = static_cast<HMODULE>(raw_module);
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return;
  const size_t directory_length = static_cast<size_t>(slash + 1 - path);
  wcscpy_s(slash + 1,
           static_cast<size_t>(MAX_PATH - directory_length),
           L"Fallout3SecondScreen.log");
  AcquireSRWLockExclusive(&g_log_lock);
  const bool opening = g_log == nullptr;
  if (opening) {
    bool rotate_existing = false;
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
      const uint64_t size =
          (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) |
          attributes.nFileSizeLow;
      rotate_existing = size >= kMaximumLogBytes;
      // Older builds opened the stream with `ccs=UTF-8` and then used the
      // narrow stdio API.  MSVCRT consequently packed pairs of ASCII bytes as
      // wide code points and emitted a UTF-8 BOM.  Detect that exact legacy
      // format so the first corrected launch starts with a parseable log.
      if (!rotate_existing && size >= 3) {
        HANDLE existing = CreateFileW(
            path, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (existing != INVALID_HANDLE_VALUE) {
          uint8_t prefix[3] = {};
          DWORD read = 0;
          if (ReadFile(existing, prefix, sizeof(prefix), &read, nullptr) &&
              read == sizeof(prefix) && prefix[0] == 0xEF &&
              prefix[1] == 0xBB && prefix[2] == 0xBF)
            rotate_existing = true;
          CloseHandle(existing);
        }
      }
      if (rotate_existing) {
        wchar_t previous[MAX_PATH] = {};
        std::memcpy(previous, path, directory_length * sizeof(wchar_t));
        wcscpy_s(previous + directory_length, MAX_PATH - directory_length,
                 L"Fallout3SecondScreen.previous.log");
        MoveFileExW(path, previous,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
      }
    }
  }
  // All messages are formatted through the narrow API.  Keep the stream byte
  // oriented as required by C stdio; selecting a wide `ccs` mode here makes
  // subsequent fprintf calls undefined and corrupts the diagnostic file.
  if (opening) _wfopen_s(&g_log, path, L"a");
  if (opening && g_log)
    WriteLocked(LogLevel::kInfo,
                "[BOOT] Fallout 3 dual-screen bootstrap initialized", 0);
  ReleaseSRWLockExclusive(&g_log_lock);
}

void Log(LogLevel level, const char* format, ...) {
  char message[2048] = {};
  va_list args;
  va_start(args, format);
  Format(message, sizeof(message), format, args);
  va_end(args);
  AcquireSRWLockExclusive(&g_log_lock);
  WriteLocked(level, message, 0);
  ReleaseSRWLockExclusive(&g_log_lock);
}

void LogRateLimited(const char* key, uint64_t interval_ms, LogLevel level,
                    const char* format, ...) {
  char message[2048] = {};
  va_list args;
  va_start(args, format);
  Format(message, sizeof(message), format, args);
  va_end(args);
  const uint32_t hash = HashKey(key);
  const uint64_t now = GetTickCount64();
  AcquireSRWLockExclusive(&g_log_lock);
  RateEntry* entry = nullptr;
  for (RateEntry& candidate : g_rates) {
    if (candidate.hash == hash) {
      entry = &candidate;
      break;
    }
    if (!candidate.hash && !entry) entry = &candidate;
  }
  if (!entry) entry = &g_rates[hash % kRateCapacity];
  if (entry->hash != hash) *entry = {hash, 0, 0};
  if (!entry->last_ms || now - entry->last_ms >= interval_ms) {
    const uint32_t suppressed = entry->suppressed;
    entry->last_ms = now;
    entry->suppressed = 0;
    WriteLocked(level, message, suppressed);
  } else {
    ++entry->suppressed;
  }
  ReleaseSRWLockExclusive(&g_log_lock);
}

void ShutdownLog() {
  AcquireSRWLockExclusive(&g_log_lock);
  if (g_log) {
    WriteLocked(LogLevel::kInfo, "[BOOT] logging shutdown", 0);
    std::fclose(g_log);
    g_log = nullptr;
  }
  ReleaseSRWLockExclusive(&g_log_lock);
}

}  // namespace fo3ds
