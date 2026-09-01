#include "IatHook.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gdds {
namespace {

bool RvaValid(uint32_t rva, uint32_t size, uint32_t image_size) {
  return rva < image_size && size <= image_size - rva;
}

bool ReadableMemory(const void* address, size_t size) {
  if (!address || !size) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
    return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
  return start >= base && size <= info.RegionSize - (start - base);
}

bool StringAtRvaEquals(const unsigned char* base, uint32_t rva,
                       uint32_t image_size, const char* expected,
                       bool insensitive) {
  if (!base || !expected || !RvaValid(rva, 1, image_size)) return false;
  const uint32_t available = image_size - rva;
  const char* actual = reinterpret_cast<const char*>(base + rva);
  for (uint32_t index = 0; index < available; ++index) {
    const unsigned char left = static_cast<unsigned char>(actual[index]);
    const unsigned char right = static_cast<unsigned char>(expected[index]);
    if (insensitive) {
      if (std::tolower(left) != std::tolower(right)) return false;
    } else if (left != right) {
      return false;
    }
    if (!left || !right) return left == right;
  }
  return false;
}

}  // namespace

void** FindIatSlot(HMODULE image, const char* imported_module,
                   const char* imported_name) {
  if (!image || !imported_module || !imported_name) return nullptr;
  auto* base = reinterpret_cast<unsigned char*>(image);
  if (!ReadableMemory(base, sizeof(IMAGE_DOS_HEADER))) return nullptr;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
      dos->e_lfanew > 1024 * 1024 ||
      !ReadableMemory(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS32)))
    return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    return nullptr;
  const uint32_t image_size = nt->OptionalHeader.SizeOfImage;
  const IMAGE_DATA_DIRECTORY& imports =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
      !RvaValid(imports.VirtualAddress, imports.Size, image_size))
    return nullptr;
  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      base + imports.VirtualAddress);
  const uint32_t descriptor_count =
      imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
  for (uint32_t descriptor_index = 0;
       descriptor_index < descriptor_count; ++descriptor_index) {
    IMAGE_IMPORT_DESCRIPTOR* current = descriptor + descriptor_index;
    if (!current->Name) return nullptr;
    if (!StringAtRvaEquals(base, current->Name, image_size, imported_module,
                           true))
      continue;
    const uint32_t names_rva = current->OriginalFirstThunk
                                   ? current->OriginalFirstThunk
                                   : current->FirstThunk;
    if (!RvaValid(names_rva, sizeof(IMAGE_THUNK_DATA32), image_size) ||
        !RvaValid(current->FirstThunk, sizeof(IMAGE_THUNK_DATA32), image_size))
      return nullptr;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + names_rva);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        base + current->FirstThunk);
    const uint32_t name_count =
        (image_size - names_rva) / sizeof(IMAGE_THUNK_DATA32);
    const uint32_t slot_count =
        (image_size - current->FirstThunk) / sizeof(IMAGE_THUNK_DATA32);
    const uint32_t thunk_count = name_count < slot_count ? name_count : slot_count;
    for (uint32_t thunk_index = 0; thunk_index < thunk_count; ++thunk_index) {
      if (!names[thunk_index].u1.AddressOfData) return nullptr;
      if (IMAGE_SNAP_BY_ORDINAL32(names[thunk_index].u1.Ordinal)) continue;
      const uint32_t name_rva = names[thunk_index].u1.AddressOfData;
      constexpr uint32_t kNameOffset = offsetof(IMAGE_IMPORT_BY_NAME, Name);
      if (!RvaValid(name_rva, kNameOffset + 1, image_size))
        return nullptr;
      if (StringAtRvaEquals(base, name_rva + kNameOffset, image_size,
                            imported_name, false))
        return reinterpret_cast<void**>(&slots[thunk_index].u1.Function);
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
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
  void* found = InterlockedCompareExchangePointer(slot, replacement, expected);
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), previous, &ignored);
  if (found != expected) return false;
  *original = expected;
  return true;
}

bool RestoreIatHook(void** slot, void* expected_replacement, void* original) {
  if (!slot || !expected_replacement || !original ||
      *slot != expected_replacement)
    return false;
  DWORD previous = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous))
    return false;
  void* found =
      InterlockedCompareExchangePointer(slot, original, expected_replacement);
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), previous, &ignored);
  return found == expected_replacement;
}

}  // namespace gdds
