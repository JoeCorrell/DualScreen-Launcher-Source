#include "Runtime.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

namespace fo4ds {
namespace {

std::wstring ModulePath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD count = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count) return {};
    if (count < buffer.size()) return std::wstring(buffer.data(), count);
    if (buffer.size() >= 32768) return {};
    buffer.resize((std::min)(buffer.size() * 2,
                             static_cast<size_t>(32768)));
  }
}

std::string FileVersion(const wchar_t* path) {
  DWORD ignored = 0;
  DWORD size = GetFileVersionInfoSizeW(path, &ignored);
  if (!size) return {};
  std::vector<uint8_t> bytes(size);
  if (!GetFileVersionInfoW(path, 0, size, bytes.data())) return {};
  VS_FIXEDFILEINFO* info = nullptr;
  UINT info_size = 0;
  if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info),
                      &info_size) || !info || info_size < sizeof(*info)) {
    return {};
  }
  char version[64] = {};
  std::snprintf(version, sizeof(version), "%u.%u.%u.%u",
                HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
  return version;
}

std::string Sha256(const wchar_t* path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD actual = 0;
  std::vector<uint8_t> object;
  std::array<uint8_t, 32> digest = {};
  HANDLE file = INVALID_HANDLE_VALUE;
  std::string output;
  constexpr DWORD kReadBufferBytes = 64 * 1024;
  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kReadBufferBytes]);
  if (!buffer) goto cleanup;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                        &actual, 0) < 0) {
    goto cleanup;
  }
  object.resize(object_size);
  if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0)
    goto cleanup;
  file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                     FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) goto cleanup;
  {
    DWORD read = 0;
    for (;;) {
      if (!ReadFile(file, buffer.get(), kReadBufferBytes, &read, nullptr))
        goto cleanup;
      if (!read) break;
      if (BCryptHashData(hash, buffer.get(), read, 0) < 0) goto cleanup;
    }
  }
  if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    goto cleanup;
  {
    static constexpr char hex[] = "0123456789ABCDEF";
    output.resize(digest.size() * 2);
    for (size_t i = 0; i < digest.size(); ++i) {
      output[i * 2] = hex[digest[i] >> 4];
      output[i * 2 + 1] = hex[digest[i] & 15];
    }
  }
cleanup:
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  return output;
}

}  // namespace

RuntimeIdentity IdentifyRuntime() {
  RuntimeIdentity identity;
  identity.path = ModulePath();
  if (identity.path.empty()) return identity;
  identity.version = FileVersion(identity.path.c_str());
  identity.sha256 = Sha256(identity.path.c_str());
  identity.supported = identity.version == "1.11.240.0" &&
      (identity.sha256 ==
           "061C1AB3738612DBC46D748F02C27795A1003501C90C15D1E649D8A07FCBBB4E" ||
       identity.sha256 ==
           "FDCEF37AC1230AF6D0B0050EB2142B139EF3A867B37B9211FB6EDFCC646072F8");
  return identity;
}

}  // namespace fo4ds
