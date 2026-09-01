#pragma once

#include <cstdint>

namespace fo3ds {

enum class LogLevel : uint8_t { kDebug, kInfo, kWarning, kError };

void InitializeLog(void* module);
void Log(LogLevel level, const char* format, ...);
void LogRateLimited(const char* key, uint64_t interval_ms, LogLevel level,
                    const char* format, ...);
void ShutdownLog();

}  // namespace fo3ds
