#include "Version.h"

#include <wincrypt.h>

#include <cstdio>
#include <cstring>
#include <limits>

namespace fo3ds {
namespace {

constexpr uint32_t kTargetTimestamp = 0x60A80E56u;  // 2021-05-21 19:47:34 UTC
constexpr uint64_t kTargetFileSize = 16855040ull;
constexpr char kTargetSha256[] =
    "c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e";

uint64_t Hash64(const uint8_t* data, size_t size) {
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool AddRange(uintptr_t base, size_t size, ModuleRange* output) {
  if (!output || !size || base > std::numeric_limits<uintptr_t>::max() - size)
    return false;
  output->begin = base;
  output->end = base + size;
  return true;
}

bool HashModuleFile(HMODULE module, uint64_t* file_size, char output[65]) {
  if (!file_size || !output) return false;
  *file_size = 0;
  output[0] = '\0';
  wchar_t path[MAX_PATH] = {};
  const DWORD path_length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!path_length || path_length >= MAX_PATH) return false;
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                            FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER length = {};
  if (!GetFileSizeEx(file, &length) || length.QuadPart < 0) {
    CloseHandle(file);
    return false;
  }
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                 CRYPT_VERIFYCONTEXT) != FALSE &&
            CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
  uint8_t buffer[64 * 1024] = {};
  while (ok) {
    DWORD count = 0;
    if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) {
      ok = false;
      break;
    }
    if (!count) break;
    if (!CryptHashData(hash, buffer, count, 0)) ok = false;
  }
  BYTE digest[32] = {};
  DWORD digest_size = sizeof(digest);
  if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) &&
               digest_size == sizeof(digest);
  if (hash) CryptDestroyHash(hash);
  if (provider) CryptReleaseContext(provider, 0);
  CloseHandle(file);
  if (!ok) return false;
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(digest); ++i) {
    output[i * 2] = kHex[digest[i] >> 4];
    output[i * 2 + 1] = kHex[digest[i] & 0x0F];
  }
  output[64] = '\0';
  *file_size = static_cast<uint64_t>(length.QuadPart);
  return true;
}

}  // namespace

bool ModuleRange::Contains(uintptr_t address, size_t bytes) const {
  if (!begin || end <= begin || !bytes || address < begin) return false;
  return address <= end && bytes <= end - address;
}

bool IsReadable(const void* raw, size_t bytes) {
  if (!raw || !bytes) return false;
  uintptr_t cursor = reinterpret_cast<uintptr_t>(raw);
  if (cursor > std::numeric_limits<uintptr_t>::max() - bytes) return false;
  const uintptr_t finish = cursor + bytes;
  while (cursor < finish) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info,
                      sizeof(info)))
      return false;
    if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
        (info.Protect & PAGE_NOACCESS))
      return false;
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE |
                           PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(info.Protect & readable)) return false;
    const uintptr_t region_end =
        reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (region_end <= cursor) return false;
    cursor = region_end < finish ? region_end : finish;
  }
  return true;
}

BuildIdentity IdentifyExecutable(HMODULE executable) {
  BuildIdentity identity = {};
  const uintptr_t base = reinterpret_cast<uintptr_t>(executable);
  if (!IsReadable(reinterpret_cast<void*>(base), sizeof(IMAGE_DOS_HEADER))) {
    std::snprintf(identity.label, sizeof(identity.label), "unreadable executable");
    return identity;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
      dos->e_lfanew > 16 * 1024 * 1024) {
    std::snprintf(identity.label, sizeof(identity.label), "invalid DOS header");
    return identity;
  }
  const uintptr_t nt_address = base + static_cast<uint32_t>(dos->e_lfanew);
  if (!IsReadable(reinterpret_cast<void*>(nt_address),
                  sizeof(IMAGE_NT_HEADERS32))) {
    std::snprintf(identity.label, sizeof(identity.label), "unreadable PE header");
    return identity;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(nt_address);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    std::snprintf(identity.label, sizeof(identity.label), "not a PE32 image");
    return identity;
  }
  identity.valid_pe = true;
  identity.is_i386 = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
  identity.timestamp = nt->FileHeader.TimeDateStamp;
  identity.image_size = nt->OptionalHeader.SizeOfImage;
  if (!identity.is_i386 || !AddRange(base, identity.image_size, &identity.image)) {
    std::snprintf(identity.label, sizeof(identity.label),
                  "PE32 validation failed machine=%04X", nt->FileHeader.Machine);
    return identity;
  }
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  if (!IsReadable(section,
                  sizeof(IMAGE_SECTION_HEADER) * nt->FileHeader.NumberOfSections)) {
    std::snprintf(identity.label, sizeof(identity.label), "invalid section table");
    return identity;
  }
  for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    char name[9] = {};
    std::memcpy(name, section[i].Name, 8);
    const uint32_t size = section[i].Misc.VirtualSize
                              ? section[i].Misc.VirtualSize
                              : section[i].SizeOfRawData;
    if (!size || section[i].VirtualAddress >= identity.image_size ||
        size > identity.image_size - section[i].VirtualAddress)
      continue;
    ModuleRange section_range = {};
    AddRange(base + section[i].VirtualAddress, size, &section_range);
    if (std::strcmp(name, ".text") == 0) {
      identity.text_rva = section[i].VirtualAddress;
      identity.text_size = size;
      identity.text = section_range;
      if (!IsReadable(reinterpret_cast<void*>(identity.text.begin),
                      identity.text_size)) {
        identity.text = {};
        identity.text_size = 0;
        continue;
      }
      identity.memory_text_hash = Hash64(
          reinterpret_cast<const uint8_t*>(identity.text.begin),
          identity.text_size);
    } else if (std::strcmp(name, ".rdata") == 0) {
      identity.rdata = section_range;
    } else if (std::strcmp(name, ".data") == 0) {
      identity.data = section_range;
    }
  }
  HashModuleFile(executable, &identity.file_size, identity.file_sha256);
  // Exact file identity is the outer compatibility gate. The in-memory text
  // hash is diagnostic only because ASLR relocations can alter mapped bytes.
  // Engine access still remains disabled until all required signatures and
  // live object invariants validate inside this exact image.
  identity.known_candidate = identity.timestamp == kTargetTimestamp &&
                             identity.file_size == kTargetFileSize &&
                             std::strcmp(identity.file_sha256,
                                         kTargetSha256) == 0 &&
                             identity.text.begin != 0 &&
                             identity.rdata.begin != 0 &&
                             identity.data.begin != 0;
  std::snprintf(identity.label, sizeof(identity.label),
                "%s PE32 ts=%08X file=%llu sha256=%.12s... text=%08X/%u memhash=%016llX",
                identity.known_candidate ? "Fallout 3 1.7.0.4 candidate" :
                                           "unknown Fallout executable",
                identity.timestamp,
                static_cast<unsigned long long>(identity.file_size),
                identity.file_sha256[0] ? identity.file_sha256 : "unavailable",
                identity.text_rva, identity.text_size,
                static_cast<unsigned long long>(identity.memory_text_hash));
  return identity;
}

uintptr_t FindPattern(const ModuleRange& range, const uint8_t* pattern,
                      const char* mask) {
  if (!pattern || !mask || !range.begin || range.end <= range.begin) return 0;
  const size_t length = std::strlen(mask);
  if (!length || static_cast<size_t>(range.end - range.begin) < length) return 0;
  const auto* bytes = reinterpret_cast<const uint8_t*>(range.begin);
  const size_t extent = static_cast<size_t>(range.end - range.begin);
  for (size_t offset = 0; offset <= extent - length; ++offset) {
    bool match = true;
    for (size_t i = 0; i < length; ++i) {
      if (mask[i] == 'x' && bytes[offset + i] != pattern[i]) {
        match = false;
        break;
      }
    }
    if (match) return range.begin + offset;
  }
  return 0;
}

}  // namespace fo3ds
