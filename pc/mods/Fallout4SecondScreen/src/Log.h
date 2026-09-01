#pragma once

#include <cstdint>

namespace fo4ds {

enum class LogLevel { kDebug, kInfo, kWarning, kError };

void InitializeLog();
void Log(LogLevel level, const char* format, ...);
void LogRateLimited(const char* key, uint64_t interval_ms, LogLevel level,
                    const char* format, ...);

}  // namespace fo4ds
