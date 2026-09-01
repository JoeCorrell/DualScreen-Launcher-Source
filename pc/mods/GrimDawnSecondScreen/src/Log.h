#pragma once

#include <windows.h>

namespace gdds {

enum class LogLevel { kInfo, kWarning, kError };

bool InitializeLog(HMODULE module);
void Log(LogLevel level, const char* format, ...);
void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...);

}  // namespace gdds
