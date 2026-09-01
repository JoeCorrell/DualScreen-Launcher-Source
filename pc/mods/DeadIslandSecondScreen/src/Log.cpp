#include "Log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace dis {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
DWORD g_start_ms = 0;
DWORD g_last_flush_ms = 0;

// Beside the executable rather than in the working directory: WEMU launches the
// guest with a working directory that is not the game folder, and a log written
// relative to it lands somewhere nobody thinks to look.
void BuildPath(wchar_t* out, size_t count, const wchar_t* filename) {
  wchar_t exe[MAX_PATH] = {};
  DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    wcsncpy(out, filename, count - 1);
    return;
  }
  wchar_t* slash = wcsrchr(exe, L'\\');
  if (slash) *(slash + 1) = L'\0';
  _snwprintf(out, count - 1, L"%s%s", exe, filename);
}

}  // namespace

void LogOpen(const wchar_t* filename) {
  if (!g_lock_ready) {
    InitializeCriticalSection(&g_lock);
    g_lock_ready = true;
  }
  if (g_file != INVALID_HANDLE_VALUE) return;

  wchar_t path[MAX_PATH * 2] = {};
  BuildPath(path, MAX_PATH * 2, filename);

  // The previous run is kept, not overwritten.
  //
  // A log that is truncated at startup means the only copy of a run's evidence
  // lives until the next launch, and a launch is exactly what happens while
  // somebody is trying to describe what they just saw. One generation back
  // costs a rename and has already been worth it.
  wchar_t previous[MAX_PATH * 2] = {};
  _snwprintf(previous, MAX_PATH * 2 - 1, L"%s.prev", path);
  DeleteFileW(previous);
  MoveFileW(path, previous);

  // FILE_SHARE_READ so the file can be tailed while the game is still running,
  // which is the only way to watch a hang.
  g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                       nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  g_start_ms = GetTickCount();
  g_last_flush_ms = g_start_ms;
}

void LogClose() {
  if (g_file != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(g_file);
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
  }
}

void LogLine(const char* level, const char* fmt, ...) {
  if (g_file == INVALID_HANDLE_VALUE || !g_lock_ready) return;

  char body[1024];
  va_list args;
  va_start(args, fmt);
  int n = _vsnprintf(body, sizeof(body), fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(body)) {
    n = static_cast<int>(sizeof(body) - 1);
  }
  body[n] = '\0';

  char line[1200];
  const DWORD now = GetTickCount();
  DWORD ms = now - g_start_ms;
  int m = _snprintf(line, sizeof(line), "[%7lu.%03lu] %s  %s\r\n",
                    ms / 1000, ms % 1000, level, body);
  if (m < 0) return;
  if (static_cast<size_t>(m) >= sizeof(line)) {
    m = static_cast<int>(sizeof(line) - 3);
    line[m++] = '\r';
    line[m++] = '\n';
    line[m] = '\0';
  }

  EnterCriticalSection(&g_lock);
  DWORD written = 0;
  WriteFile(g_file, line, static_cast<DWORD>(m), &written, nullptr);
  // Shared Android storage makes FlushFileBuffers extraordinarily expensive.
  // Preserve the last useful evidence on errors, but batch routine diagnostics
  // to one durable flush per second so a noisy subsystem cannot freeze Wine.
  if ((level && level[0] == 'e') || now - g_last_flush_ms >= 1000) {
    FlushFileBuffers(g_file);
    g_last_flush_ms = now;
  }
  LeaveCriticalSection(&g_lock);
}

}  // namespace dis
