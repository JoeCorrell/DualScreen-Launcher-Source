#pragma once

#include <string>

namespace fo4ds {

struct RuntimeIdentity {
  std::wstring path;
  std::string version;
  std::string sha256;
  bool supported = false;
};

RuntimeIdentity IdentifyRuntime();

}  // namespace fo4ds
