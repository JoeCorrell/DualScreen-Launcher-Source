#include "Config.h"

#include <windows.h>
#include <cstring>
#include <cstdlib>

#include "Log.h"

namespace dis {
namespace {

Config g_config;

void CopyTerminated(char* destination, size_t destination_size,
                    const char* source) {
  if (!destination || destination_size == 0) return;

  destination[0] = '\0';
  if (!source) return;

  size_t length = std::strlen(source);
  if (length >= destination_size) length = destination_size - 1;
  if (length != 0) std::memcpy(destination, source, length);
  destination[length] = '\0';
}

template <size_t N>
void ToWide(const char* source, wchar_t (&destination)[N]) {
  destination[0] = L'\0';
  if (!source || MultiByteToWideChar(CP_ACP, 0, source, -1, destination,
                                      static_cast<int>(N)) == 0) {
    destination[0] = L'\0';
  }
  destination[N - 1] = L'\0';
}

template <size_t N>
void ToNarrow(const wchar_t* source, char (&destination)[N]) {
  destination[0] = '\0';
  if (!source || WideCharToMultiByte(CP_ACP, 0, source, -1, destination,
                                     static_cast<int>(N), nullptr,
                                     nullptr) == 0) {
    destination[0] = '\0';
  }
  destination[N - 1] = '\0';
}

// "panel.width" -> section "panel", key "width". A flat key space reads better
// at the call site than a pair of arguments and keeps the ini conventional.
void Split(const char* key, char* section, size_t sn, char* name, size_t nn) {
  if (!key) key = "";
  const char* dot = strchr(key, '.');
  if (!dot) {
    CopyTerminated(section, sn, "general");
    CopyTerminated(name, nn, key);
    return;
  }

  size_t sl = static_cast<size_t>(dot - key);
  if (section && sn != 0) {
    if (sl >= sn) sl = sn - 1;
    if (sl != 0) std::memcpy(section, key, sl);
    section[sl] = '\0';
  }
  CopyTerminated(name, nn, dot + 1);
}

}  // namespace

Config& Cfg() { return g_config; }

void Config::Load() {
  wchar_t exe[MAX_PATH] = {};
  DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(exe, L'\\');
  if (slash) *(slash + 1) = L'\0';

  wchar_t full[MAX_PATH * 2] = {};
  _snwprintf(full, MAX_PATH * 2 - 1, L"%sDeadIslandSecondScreen.ini", exe);
  full[MAX_PATH * 2 - 1] = L'\0';
  path_ = full;
  present_ = GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES;

  if (!present_) {
    DIS_WARN("[cfg] no DeadIslandSecondScreen.ini beside the executable; using defaults");
  }
}

int Config::GetInt(const char* key, int fallback) const {
  char section[64] = {}, name[96] = {};
  Split(key, section, sizeof(section), name, sizeof(name));
  wchar_t ws[64] = {}, wn[96] = {};
  ToWide(section, ws);
  ToWide(name, wn);
  return static_cast<int>(GetPrivateProfileIntW(ws, wn, fallback, path_.c_str()));
}

bool Config::GetBool(const char* key, bool fallback) const {
  return GetInt(key, fallback ? 1 : 0) != 0;
}

std::string Config::GetString(const char* key, const char* fallback) const {
  char section[64] = {}, name[96] = {};
  Split(key, section, sizeof(section), name, sizeof(name));
  wchar_t ws[64] = {}, wn[96] = {}, out[256] = {};
  ToWide(section, ws);
  ToWide(name, wn);
  wchar_t wfallback[256] = {};
  ToWide(fallback ? fallback : "", wfallback);
  GetPrivateProfileStringW(ws, wn, wfallback, out, 256, path_.c_str());
  out[255] = L'\0';
  char narrow[256] = {};
  ToNarrow(out, narrow);
  return std::string(narrow);
}

float Config::GetFloat(const char* key, float fallback) const {
  char buffer[64] = {};
  _snprintf(buffer, sizeof(buffer) - 1, "%f", fallback);
  buffer[sizeof(buffer) - 1] = '\0';
  std::string text = GetString(key, buffer);
  return static_cast<float>(atof(text.c_str()));
}

std::string Config::Route(const std::string& controller, const char* fallback) const {
  wchar_t wn[192] = {}, out[64] = {}, wfallback[64] = {};
  ToWide(controller.c_str(), wn);
  ToWide(fallback ? fallback : "", wfallback);
  GetPrivateProfileStringW(L"route", wn, wfallback, out, 64, path_.c_str());
  out[63] = L'\0';
  char narrow[64] = {};
  ToNarrow(out, narrow);
  return std::string(narrow);
}

}  // namespace dis
