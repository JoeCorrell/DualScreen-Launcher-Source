// The log is the deliverable for a diagnostic run. Errors are flushed
// immediately and routine lines are made durable once per second; flushing
// every line can stall Wine when the game folder is on Android shared storage.

#pragma once

namespace dis {

// Opens <game folder>\DeadIslandSecondScreen.log, truncating a previous run.
void LogOpen(const wchar_t* filename);
void LogClose();
void LogLine(const char* level, const char* fmt, ...);

}  // namespace dis

#define DIS_INFO(...) ::dis::LogLine("info", __VA_ARGS__)
#define DIS_WARN(...) ::dis::LogLine("warn", __VA_ARGS__)
#define DIS_ERR(...) ::dis::LogLine("err ", __VA_ARGS__)
