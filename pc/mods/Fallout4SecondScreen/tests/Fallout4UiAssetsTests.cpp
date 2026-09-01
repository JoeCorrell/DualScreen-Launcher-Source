#include <windows.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Fallout4UiAssets.h"

namespace {

std::wstring ExecutableDirectory() {
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                          static_cast<DWORD>(path.size()));
  assert(length && length < path.size());
  std::wstring result(path.data(), length);
  result.resize(result.find_last_of(L"\\/"));
  return result;
}

void WriteSyntheticRgba(const std::wstring& path) {
  const uint8_t bytes[] = {'R', 'G', 'B', 'A', 2, 0, 0, 0, 2, 0, 0, 0,
                           255, 255, 255, 255, 255, 255, 255, 255,
                           255, 255, 255, 255, 255, 255, 255, 255};
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  assert(file != INVALID_HANDLE_VALUE);
  DWORD written = 0;
  assert(WriteFile(file, bytes, sizeof(bytes), &written, nullptr));
  assert(written == sizeof(bytes));
  CloseHandle(file);
}

uint64_t Fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

std::wstring Hex16(uint64_t value) {
  constexpr wchar_t digits[] = L"0123456789abcdef";
  std::wstring result(16, L'0');
  for (int index = 15; index >= 0; --index) {
    result[static_cast<size_t>(index)] = digits[value & 0xfu];
    value >>= 4;
  }
  return result;
}

}  // namespace

int main() {
  using namespace fo4ds;
  const std::wstring base = ExecutableDirectory() +
                            L"\\Fallout4SecondScreen.assets";
  const std::wstring special = base + L"\\special";
  const std::wstring perks = base + L"\\perks";
  CreateDirectoryW(base.c_str(), nullptr);
  CreateDirectoryW(special.c_str(), nullptr);
  CreateDirectoryW(perks.c_str(), nullptr);
  const std::wstring image_path = special + L"\\0.rgba";
  const std::string magazine_path =
      "interface\\components\\magazine perks\\mp_live_love.swf";
  const std::wstring perk_path =
      perks + L"\\" + Hex16(Fnv1a64(magazine_path)) + L".rgba";
  WriteSyntheticRgba(image_path);
  WriteSyntheticRgba(perk_path);

  HDC target = CreateCompatibleDC(nullptr);
  assert(target);
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = 8;
  info.bmiHeader.biHeight = -8;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(target, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  assert(bitmap && bits);
  HGDIOBJ previous = SelectObject(target, bitmap);
  assert(previous && previous != HGDI_ERROR);
  std::memset(bits, 0, 8 * 8 * 4);

  assert(DrawFallout4SpecialArt(target, 0, {0, 0, 8, 8}));
  assert(!DrawFallout4SpecialArt(target, 7, {0, 0, 8, 8}));
  assert(!DrawFallout4PerkArt(target, 0, 0, nullptr, {0, 0, 8, 8}));
  assert(DrawFallout4PerkArt(
      target, 0, 0, "Components/VaultBoys/../Magazine perks/invalid.swf",
      {0, 0, 8, 8}) == false);
  assert(DrawFallout4PerkArt(
      target, 0, 0, "Components/Magazine perks/MP_Live_Love.swf",
      {0, 0, 8, 8}));
  const uint8_t* pixels = static_cast<const uint8_t*>(bits);
  bool nonzero = false;
  for (size_t index = 0; index < 8 * 8 * 4; ++index)
    nonzero = nonzero || pixels[index] != 0;
  assert(nonzero);

  ShutdownFallout4UiAssets();
  SelectObject(target, previous);
  DeleteObject(bitmap);
  DeleteDC(target);
  assert(DeleteFileW(image_path.c_str()));
  assert(DeleteFileW(perk_path.c_str()));
  assert(RemoveDirectoryW(special.c_str()));
  assert(RemoveDirectoryW(perks.c_str()));
  assert(RemoveDirectoryW(base.c_str()));
  std::cout << "Fallout4UiAssetsTests passed\n";
  return 0;
}
