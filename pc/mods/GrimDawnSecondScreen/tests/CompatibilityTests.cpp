#include <windows.h>

#include <cstdio>
#include <cstring>

#include "Compatibility.h"

namespace {

constexpr size_t kImageSize = 0x200000;
constexpr size_t kSite = 0x17ab01;
constexpr size_t kCave = 0x17ab35;

int g_failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++g_failures;
}

unsigned char* MakeImage() {
  auto* image = static_cast<unsigned char*>(VirtualAlloc(
      nullptr, kImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!image) return nullptr;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
  dos->e_magic = IMAGE_DOS_SIGNATURE;
  dos->e_lfanew = 0x100;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
  nt->Signature = IMAGE_NT_SIGNATURE;
  nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
  nt->OptionalHeader.SizeOfImage = kImageSize;
  const unsigned char site[] = {0xff, 0x75, 0x18, 0x8b, 0x06};
  std::memcpy(image + kSite, site, sizeof(site));
  std::memset(image + kCave, 0xcc, 11);
  return image;
}

}  // namespace

int main() {
  unsigned char* image = MakeImage();
  Check(image != nullptr, "synthetic exact-build image allocated");
  if (image) {
    Check(gdds::InstallStartupAudioGuard(
              reinterpret_cast<HMODULE>(image)),
          "exact signatures accepted");
    const unsigned char jump[] = {0xe9, 0x2f, 0x00, 0x00, 0x00};
    const unsigned char cave[] = {0x85, 0xf6, 0x74, 0xf3, 0xff, 0x75,
                                  0x18, 0x8b, 0x06, 0xeb, 0xc6};
    Check(std::memcmp(image + kSite, jump, sizeof(jump)) == 0,
          "live site redirects into guard cave");
    Check(std::memcmp(image + kCave, cave, sizeof(cave)) == 0,
          "guard cave preserves non-null path and skips null path");
    VirtualFree(image, 0, MEM_RELEASE);
  }

  image = MakeImage();
  if (image) {
    image[kSite] = 0x90;
    Check(!gdds::InstallStartupAudioGuard(
               reinterpret_cast<HMODULE>(image)),
          "signature mismatch fails closed");
    Check(image[kCave] == 0xcc, "failed validation leaves cave untouched");
    VirtualFree(image, 0, MEM_RELEASE);
  }

  if (g_failures) return 1;
  std::puts("CompatibilityTests passed");
  return 0;
}
