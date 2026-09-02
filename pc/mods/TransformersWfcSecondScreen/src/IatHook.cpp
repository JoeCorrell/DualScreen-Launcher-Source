#include "IatHook.h"

#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>

namespace wfcds {
namespace {

bool ValidRva(uint32_t rva, uint32_t bytes, uint32_t image_size) {
  return rva < image_size && bytes <= image_size - rva;
}

bool Equal(const unsigned char* base, uint32_t rva, uint32_t image_size,
           const char* expected, bool insensitive) {
  if (!base || !expected || !ValidRva(rva, 1, image_size)) return false;
  const char* actual = reinterpret_cast<const char*>(base + rva);
  for (uint32_t i = 0; i < image_size - rva; ++i) {
    unsigned char left = static_cast<unsigned char>(actual[i]);
    unsigned char right = static_cast<unsigned char>(expected[i]);
    if (insensitive) {
      left = static_cast<unsigned char>(std::tolower(left));
      right = static_cast<unsigned char>(std::tolower(right));
    }
    if (left != right) return false;
    if (!left) return true;
  }
  return false;
}

}  // namespace

void** FindIatSlot(HMODULE image, const char* module, const char* name) {
  if (!image || !module || !name) return nullptr;
  auto* base = reinterpret_cast<unsigned char*>(image);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    return nullptr;
  const uint32_t image_size = nt->OptionalHeader.SizeOfImage;
  const IMAGE_DATA_DIRECTORY& directory =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!ValidRva(directory.VirtualAddress, directory.Size, image_size))
    return nullptr;
  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      base + directory.VirtualAddress);
  const uint32_t descriptors = directory.Size / sizeof(*descriptor);
  for (uint32_t d = 0; d < descriptors && descriptor[d].Name; ++d) {
    if (!Equal(base, descriptor[d].Name, image_size, module, true)) continue;
    const uint32_t names_rva = descriptor[d].OriginalFirstThunk
                                   ? descriptor[d].OriginalFirstThunk
                                   : descriptor[d].FirstThunk;
    if (!ValidRva(names_rva, sizeof(IMAGE_THUNK_DATA32), image_size) ||
        !ValidRva(descriptor[d].FirstThunk, sizeof(IMAGE_THUNK_DATA32),
                  image_size))
      return nullptr;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + names_rva);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        base + descriptor[d].FirstThunk);
    const uint32_t limit = (image_size - names_rva) / sizeof(*names);
    for (uint32_t i = 0; i < limit && names[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal)) continue;
      const uint32_t entry = names[i].u1.AddressOfData;
      constexpr uint32_t kName = offsetof(IMAGE_IMPORT_BY_NAME, Name);
      if (!ValidRva(entry, kName + 1, image_size)) return nullptr;
      if (Equal(base, entry + kName, image_size, name, false))
        return reinterpret_cast<void**>(&slots[i].u1.Function);
    }
    return nullptr;
  }
  return nullptr;
}

bool InstallIatHook(void** slot, void* expected, void* replacement,
                    void** original) {
  if (!slot || !expected || !replacement || !original || *slot != expected)
    return false;
  DWORD previous = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
    return false;
  // Once the slot changes, another thread may enter the replacement
  // immediately. Publish its original call target before exposing it.
  *original = expected;
  std::atomic_thread_fence(std::memory_order_release);
  void* found = InterlockedCompareExchangePointer(slot, replacement, expected);
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), previous, &ignored);
  if (found != expected) {
    *original = nullptr;
    return false;
  }
  return true;
}

bool RestoreIatHook(void** slot, void* replacement, void* original) {
  if (!slot || !replacement || !original || *slot != replacement) return false;
  DWORD previous = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
    return false;
  void* found = InterlockedCompareExchangePointer(slot, original, replacement);
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), previous, &ignored);
  return found == replacement;
}

}  // namespace wfcds
