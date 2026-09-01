// DeadIslandSecondScreen.ini, read once beside the executable.
//
// Every number this mod could get wrong is here rather than compiled in, and
// the routing table is here too. Not for tweakability's sake: this runs on a
// handheld under Wine where a rebuild is a twenty-minute round trip, and an
// element that turns out to belong on the other screen should be one line in a
// text file, not a new DLL.

#pragma once

#include <string>

namespace dis {

class Config {
 public:
  void Load();

  int GetInt(const char* key, int fallback) const;
  bool GetBool(const char* key, bool fallback) const;
  float GetFloat(const char* key, float fallback) const;
  std::string GetString(const char* key, const char* fallback) const;

  // The [route] section, which is keyed by controller class name rather than by
  // an index, so an entry survives a game patch that reorders anything.
  std::string Route(const std::string& controller, const char* fallback) const;

  const std::wstring& path() const { return path_; }
  bool present() const { return present_; }

 private:
  std::wstring path_;
  bool present_ = false;
};

Config& Cfg();

}  // namespace dis
