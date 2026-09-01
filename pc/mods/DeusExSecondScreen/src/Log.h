// The log is the deliverable for a diagnostic run, so it is written to survive
// a crash: opened once, appended, and flushed after every line. A buffered log
// that loses the last twenty lines loses precisely the ones that matter.

#pragma once

namespace dxs {

// Opens <game folder>\DeusExSecondScreen.log, truncating a previous run.
void LogOpen(const wchar_t* filename);
void LogClose();
void LogLine(const char* level, const char* fmt, ...);

}  // namespace dxs

#define DXS_INFO(...) ::dxs::LogLine("info", __VA_ARGS__)
#define DXS_WARN(...) ::dxs::LogLine("warn", __VA_ARGS__)
#define DXS_ERR(...) ::dxs::LogLine("err ", __VA_ARGS__)
