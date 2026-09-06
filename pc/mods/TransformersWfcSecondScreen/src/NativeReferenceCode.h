#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace wfcds {
namespace native_reference_code {
constexpr uintptr_t kImageBase = 0x10900000u;
constexpr uintptr_t kBarrierVa = 0x116FC820u;
// Entire WFC object-reference assignment barrier, cdecl(UObject*). Preserves
// EDX (compiled setters rely on it), marks the GC rescan flag when required.
constexpr uint8_t kBarrier[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x45, 0x08, 0x85, 0xC0, 0x74,
    0x21, 0x83, 0x3D, 0x7C, 0x04, 0xB1, 0x12, 0x03, 0x7F, 0x18, 0x8B, 0x40,
    0x08, 0x25, 0x00, 0x00, 0x80, 0x00, 0x33, 0xC9, 0x0B, 0xC1, 0x74, 0x0A,
    0xC7, 0x05, 0x74, 0x04, 0xB1, 0x12, 0x01, 0x00, 0x00, 0x00, 0x8B, 0xE5,
    0x5D, 0xC3,
};
// Relocate only independently verified absolute operands. Relative calls do
// not change when the complete PE image moves; never wildcard instruction bytes.
template<size_t N>
std::array<uint8_t, N> Relocate(const uint8_t (&code)[N], uintptr_t image,
                               std::initializer_list<size_t> operands) {
  std::array<uint8_t, N> result;
  std::memcpy(result.data(), code, N);
  for (const auto offset : operands) {
    uint32_t value = 0;
    if (offset + sizeof(value) > N) return {};
    std::memcpy(&value, result.data() + offset, sizeof(value));
    value += static_cast<uint32_t>(image - kImageBase);
    std::memcpy(result.data() + offset, &value, sizeof(value));
  }
  return result;
}
}  // namespace native_reference_code
}  // namespace wfcds
