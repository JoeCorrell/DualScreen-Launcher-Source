#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "PeExports.h"

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) {
    std::fwprintf(stderr, L"usage: ProxyAbiTests original proxy\n");
    return 2;
  }
  std::vector<PeExport> original;
  std::vector<PeExport> proxy;
  std::string error;
  if (!ReadPeExports(argv[1], &original, &error) ||
      !ReadPeExports(argv[2], &proxy, &error)) {
    std::fprintf(stderr, "PE parse failed: %s\n", error.c_str());
    return 1;
  }
  auto order = [](const PeExport& left, const PeExport& right) {
    return left.ordinal < right.ordinal;
  };
  std::sort(original.begin(), original.end(), order);
  std::sort(proxy.begin(), proxy.end(), order);
  if (original.size() != 1271 || proxy.size() != original.size()) {
    std::fprintf(stderr, "export count mismatch original=%zu proxy=%zu\n",
                 original.size(), proxy.size());
    return 1;
  }
  for (std::size_t index = 0; index < original.size(); ++index) {
    const PeExport& expected = original[index];
    const PeExport& actual = proxy[index];
    const std::string forward =
        "SDL3_FieldsOfMistriaOriginal." + expected.name;
    if (expected.ordinal != actual.ordinal || expected.name != actual.name ||
        actual.forwarder != forward) {
      std::fprintf(stderr,
                   "export mismatch at %zu expected=%s@%u actual=%s@%u -> %s\n",
                   index, expected.name.c_str(), expected.ordinal,
                   actual.name.c_str(), actual.ordinal,
                   actual.forwarder.c_str());
      return 1;
    }
  }
  std::puts("ProxyAbiTests passed: 1271 exact named/ordinal forwarders");
  return 0;
}
