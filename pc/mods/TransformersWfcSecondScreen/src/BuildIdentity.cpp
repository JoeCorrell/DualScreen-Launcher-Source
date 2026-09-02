#include "BuildIdentity.h"

#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace wfcds {
namespace {

constexpr uint64_t kExpectedBytes = 37667328ull;
constexpr uint32_t kExpectedTimestamp = 0x4BF33BE6u;
constexpr uint32_t kExpectedImageSize = 0x02560000u;
constexpr char kExpectedSha256[] =
    "1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016";

bool HashFile(const wchar_t* path, char output[65], uint64_t* file_size) {
  if (!path || !output || !file_size) return false;
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ |
                            FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
    CloseHandle(file);
    return false;
  }
  *file_size = static_cast<uint64_t>(size.QuadPart);

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_bytes = 0;
  DWORD result_bytes = 0;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) == 0 &&
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_bytes),
                              sizeof(object_bytes), &result_bytes, 0) == 0;
  std::vector<unsigned char> object(object_bytes);
  if (ok)
    ok = BCryptCreateHash(algorithm, &hash, object.data(), object_bytes,
                          nullptr, 0, 0) == 0;
  std::vector<unsigned char> buffer(1024 * 1024);
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr)) {
      ok = false;
      break;
    }
    if (read == 0) break;
    ok = BCryptHashData(hash, buffer.data(), read, 0) == 0;
  }
  unsigned char digest[32] = {};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) return false;
  for (size_t i = 0; i < sizeof(digest); ++i)
    std::snprintf(output + i * 2, 3, "%02x", digest[i]);
  output[64] = '\0';
  return true;
}

}  // namespace

BuildIdentity IdentifyExecutable(HMODULE image) {
  BuildIdentity result;
  wchar_t path[MAX_PATH] = {};
  if (!image || !GetModuleFileNameW(image, path, MAX_PATH)) {
    std::snprintf(result.label, sizeof(result.label), "unreadable executable");
    return result;
  }
  const auto* base = reinterpret_cast<const unsigned char*>(image);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    std::snprintf(result.label, sizeof(result.label), "invalid DOS header");
    return result;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    std::snprintf(result.label, sizeof(result.label), "not a PE32 executable");
    return result;
  }
  result.machine = nt->FileHeader.Machine;
  result.timestamp = nt->FileHeader.TimeDateStamp;
  result.image_size = nt->OptionalHeader.SizeOfImage;
  if (!HashFile(path, result.sha256, &result.file_size)) {
    std::snprintf(result.label, sizeof(result.label), "executable hash failed");
    return result;
  }
  result.supported = result.machine == IMAGE_FILE_MACHINE_I386 &&
                     result.timestamp == kExpectedTimestamp &&
                     result.image_size == kExpectedImageSize &&
                     result.file_size == kExpectedBytes &&
                     std::strcmp(result.sha256, kExpectedSha256) == 0;
  std::snprintf(result.label, sizeof(result.label),
                result.supported ? "WFC PC 1.0 (Steam 42650)" :
                                   "unsupported TWFC executable");
  return result;
}

}  // namespace wfcds
