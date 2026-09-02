#pragma once

#include <windows.h>

namespace wfcds {

enum class LogLevel { kDebug, kInfo, kWarning, kError };

void InitializeLog(HMODULE module);
void ShutdownLog();
void Log(LogLevel level, const char* format, ...);
void LogEmergency(LogLevel level, const char* format, ...);
void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...);

}  // namespace wfcds
