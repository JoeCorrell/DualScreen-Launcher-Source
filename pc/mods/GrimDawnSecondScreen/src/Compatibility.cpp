#include "Compatibility.h"

#include <cstdint>
#include <cstring>

#include "Log.h"

namespace gdds {
namespace {

constexpr uintptr_t kFloatStreamSiteRva = 0x17ab01;
constexpr uintptr_t kFloatStreamCaveRva = 0x17ab35;

constexpr unsigned char kExpectedSite[] = {
    0xff, 0x75, 0x18, 0x8b, 0x06};
constexpr unsigned char kExpectedCave[] = {
    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
    0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
constexpr unsigned char kGuardCave[] = {
    0x85, 0xf6,        // test esi, esi
    0x74, 0xf3,        // jz normal epilogue at Engine+0x17ab2c
    0xff, 0x75, 0x18,  // displaced push [ebp+0x18]
    0x8b, 0x06,        // displaced mov eax, [esi]
    0xeb, 0xc6};       // jmp Engine+0x17ab06
constexpr unsigned char kSiteJump[] = {
    0xe9, 0x2f, 0x00, 0x00, 0x00};

bool ImageRange(HMODULE module, uintptr_t rva, size_t size,
                unsigned char** output) {
  if (!module || !output || !size) return false;
  auto* base = reinterpret_cast<unsigned char*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      rva >= nt->OptionalHeader.SizeOfImage ||
      size > nt->OptionalHeader.SizeOfImage - rva)
    return false;
  *output = base + rva;
  return true;
}

struct CodeWriteResult {
  bool published = false;
  bool cache_flushed = false;
  bool protection_restored = false;
};

CodeWriteResult WriteCode(unsigned char* destination,
                          const unsigned char* source, size_t size) {
  CodeWriteResult result;
  DWORD old_protection = 0;
  if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE,
                      &old_protection))
    return result;
  std::memcpy(destination, source, size);
  result.published = true;
  result.cache_flushed =
      FlushInstructionCache(GetCurrentProcess(), destination, size) != FALSE;
  DWORD ignored = 0;
  result.protection_restored =
      VirtualProtect(destination, size, old_protection, &ignored) != FALSE;
  return result;
}

}  // namespace

bool InstallStartupAudioGuard(HMODULE engine) {
  unsigned char* site = nullptr;
  unsigned char* cave = nullptr;
  if (!ImageRange(engine, kFloatStreamSiteRva, sizeof(kExpectedSite), &site) ||
      !ImageRange(engine, kFloatStreamCaveRva, sizeof(kExpectedCave), &cave)) {
    Log(LogLevel::kError,
        "[COMPAT] startup audio guard refused: Engine image ranges invalid");
    return false;
  }
  if (std::memcmp(site, kExpectedSite, sizeof(kExpectedSite)) != 0 ||
      std::memcmp(cave, kExpectedCave, sizeof(kExpectedCave)) != 0) {
    Log(LogLevel::kError,
        "[COMPAT] startup audio guard refused: exact instruction signature mismatch site=%p cave=%p",
        site, cave);
    return false;
  }

  // The cave is unreachable padding. Publish it before changing the live site.
  const CodeWriteResult cave_write =
      WriteCode(cave, kGuardCave, sizeof(kGuardCave));
  if (!cave_write.published) {
    Log(LogLevel::kError,
        "[COMPAT] startup audio guard refused: cave write failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  if (!cave_write.cache_flushed || !cave_write.protection_restored) {
    const CodeWriteResult rollback =
        WriteCode(cave, kExpectedCave, sizeof(kExpectedCave));
    Log(LogLevel::kError,
        "[COMPAT] startup audio guard refused before live-site publication: cave cache_flush=%u protection_restore=%u rollback=%u/%u/%u",
        cave_write.cache_flushed ? 1u : 0u,
        cave_write.protection_restored ? 1u : 0u,
        rollback.published ? 1u : 0u,
        rollback.cache_flushed ? 1u : 0u,
        rollback.protection_restored ? 1u : 0u);
    return false;
  }

  const CodeWriteResult site_write =
      WriteCode(site, kSiteJump, sizeof(kSiteJump));
  if (!site_write.published) {
    WriteCode(cave, kExpectedCave, sizeof(kExpectedCave));
    Log(LogLevel::kError,
        "[COMPAT] startup audio guard refused: site write failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  // Once the site jump is visible the cave must never be restored: doing so
  // would turn a recoverable protection error into a jump into INT3 padding.
  if (!site_write.cache_flushed || !site_write.protection_restored) {
    Log(LogLevel::kWarning,
        "[COMPAT] site published with cache_flush=%u protection_restore=%u; guard remains installed",
        site_write.cache_flushed ? 1u : 0u,
        site_write.protection_restored ? 1u : 0u);
  }
  if (std::memcmp(site, kSiteJump, sizeof(kSiteJump)) != 0 ||
      std::memcmp(cave, kGuardCave, sizeof(kGuardCave)) != 0) {
    Log(LogLevel::kError,
        "[COMPAT] post-write verification failed; refusing further initialization");
    return false;
  }
  Log(LogLevel::kInfo,
      "[COMPAT] installed exact-build null external-stream guard site=%p cave=%p; only missing startup-video audio packets are dropped",
      site, cave);
  return true;
}

}  // namespace gdds
