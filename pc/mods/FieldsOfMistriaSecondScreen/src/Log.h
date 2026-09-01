#pragma once

#include <windows.h>

namespace fomds {

enum class LogLevel { kInfo, kWarning, kError };

bool InitializeLog(HMODULE module);
bool InitializeNamedLog(HMODULE module, const wchar_t* file_name,
                        const wchar_t* previous_file_name);
void Log(LogLevel level, const char* format, ...);
void LogRateLimited(const char* key, DWORD interval_ms, LogLevel level,
                    const char* format, ...);

}  // namespace fomds
