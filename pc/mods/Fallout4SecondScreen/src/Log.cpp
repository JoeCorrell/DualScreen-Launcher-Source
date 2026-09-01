#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fo4ds {
namespace {

std::mutex g_mutex;
std::unordered_map<std::string, uint64_t> g_last_log;
std::wstring g_path;

const char* Name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarning: return "WARN";
    case LogLevel::kError: return "ERROR";
  }
  return "INFO";
}

void WriteLocked(LogLevel level, const char* message) {
  SYSTEMTIME time = {};
  GetLocalTime(&time);
  char line[2304] = {};
  std::snprintf(line, sizeof(line),
                "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] [tid=%lu] %s\r\n",
                time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                time.wSecond, time.wMilliseconds, Name(level),
                static_cast<unsigned long>(GetCurrentThreadId()), message);
  OutputDebugStringA(line);
  if (g_path.empty()) return;
  HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
  CloseHandle(file);
}

void FormatAndWrite(LogLevel level, const char* format, va_list args) {
  char message[2048] = {};
  std::vsnprintf(message, sizeof(message), format, args);
  WriteLocked(level, message);
}

}  // namespace

void InitializeLog() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_path.empty()) return;
  wchar_t module[MAX_PATH] = {};
  if (!GetModuleFileNameW(nullptr, module, MAX_PATH)) return;
  wchar_t* slash = std::wcsrchr(module, L'\\');
  if (!slash) return;
  slash[1] = L'\0';
  g_path = module;
  g_path += L"Fallout4SecondScreen.log";
}

void Log(LogLevel level, const char* format, ...) {
  std::lock_guard<std::mutex> lock(g_mutex);
  va_list args;
  va_start(args, format);
  FormatAndWrite(level, format, args);
  va_end(args);
}

void LogRateLimited(const char* key, uint64_t interval_ms, LogLevel level,
                    const char* format, ...) {
  const uint64_t now = GetTickCount64();
  std::lock_guard<std::mutex> lock(g_mutex);
  uint64_t& last = g_last_log[key ? key : ""];
  if (last && now - last < interval_ms) return;
  last = now;
  va_list args;
  va_start(args, format);
  FormatAndWrite(level, format, args);
  va_end(args);
}

}  // namespace fo4ds
