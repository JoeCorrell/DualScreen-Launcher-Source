#include "BuildIdentity.h"

#include <bcrypt.h>
#include <winver.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include "ExpectedBuild.h"

namespace fomds {
namespace {

bool HashFile(const wchar_t* path, FileIdentity* identity) {
  if (!path || !identity) return false;
  HANDLE file = CreateFileW(path, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD transferred = 0;
  if (ok) {
    ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                     nullptr, 0) >= 0 &&
         BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                           reinterpret_cast<PUCHAR>(&object_size),
                           sizeof(object_size), &transferred, 0) >= 0;
  }
  std::vector<unsigned char> object(object_size);
  if (ok) {
    ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                          nullptr, 0, 0) >= 0;
  }
  // This runs on the bootstrap worker. Keep the bulk read buffer off its
  // image-default stack: Wine/FEX configurations can provide considerably
  // less stack headroom than native Windows.
  std::vector<unsigned char> buffer(256 * 1024);
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr)) {
      ok = false;
      break;
    }
    if (!read) break;
    ok = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
  }
  unsigned char digest[32] = {};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) return false;

  identity->present = true;
  identity->size = static_cast<std::uint64_t>(size.QuadPart);
  for (std::size_t index = 0; index < sizeof(digest); ++index) {
    std::snprintf(identity->sha256 + index * 2,
                  sizeof(identity->sha256) - index * 2, "%02x", digest[index]);
  }
  return true;
}

void ReadVersion(const wchar_t* path, char* output, std::size_t capacity) {
  if (!path || !output || !capacity) return;
  DWORD ignored = 0;
  const DWORD bytes = GetFileVersionInfoSizeW(path, &ignored);
  if (!bytes) return;
  std::vector<unsigned char> data(bytes);
  if (!GetFileVersionInfoW(path, 0, bytes, data.data())) return;
  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT length = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed),
                      &length) ||
      !fixed || length < sizeof(VS_FIXEDFILEINFO)) {
    return;
  }
  std::snprintf(output, capacity, "%u.%u.%u.%u",
                HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
}

bool Pe64(const wchar_t* path) {
  HANDLE file = CreateFileW(path, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  IMAGE_DOS_HEADER dos = {};
  DWORD read = 0;
  bool ok = ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
            read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew > 0;
  IMAGE_NT_HEADERS64 nt = {};
  if (ok) {
    LARGE_INTEGER position = {};
    position.QuadPart = dos.e_lfanew;
    ok = SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
         ReadFile(file, &nt, sizeof(nt), &read, nullptr) &&
         read == sizeof(nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
         nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
         nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  }
  CloseHandle(file);
  return ok;
}

bool Same(const char* left, const char* right) {
  return left && right && _stricmp(left, right) == 0;
}

bool ModulePath(HMODULE module, std::wstring* output) {
  if (!output) return false;
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      module, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) return false;
  output->assign(buffer.data(), length);
  return true;
}

}  // namespace

BuildIdentity IdentifyBuildFromPaths(const wchar_t* executable_path,
                                     const wchar_t* sdl_path) {
  BuildIdentity result;
  if (!executable_path || !sdl_path) {
    std::snprintf(result.reason, sizeof(result.reason), "missing build paths");
    return result;
  }
  result.valid_pe64 = Pe64(executable_path) && Pe64(sdl_path);
  HashFile(executable_path, &result.executable);
  HashFile(sdl_path, &result.sdl);
  ReadVersion(executable_path, result.executable.version,
              sizeof(result.executable.version));
  ReadVersion(sdl_path, result.sdl.version, sizeof(result.sdl.version));

  result.supported = result.valid_pe64 && result.executable.present &&
      result.sdl.present && result.executable.size == kExpectedGameSize &&
      result.sdl.size == kExpectedSdlSize &&
      Same(result.executable.sha256, kExpectedGameSha256) &&
      Same(result.sdl.sha256, kExpectedSdlSha256) &&
      Same(result.executable.version, kExpectedGameFileVersion) &&
      Same(result.sdl.version, kExpectedSdlVersion);
  if (result.supported) {
    std::snprintf(result.reason, sizeof(result.reason),
                  "supported Fields of Mistria 1.0.4 / SDL 3.4.8 x64");
  } else {
    std::snprintf(
        result.reason, sizeof(result.reason),
        "unsupported build pe64=%u exe=%s/%llu/%s sdl=%s/%llu/%s",
        result.valid_pe64 ? 1u : 0u, result.executable.version,
        static_cast<unsigned long long>(result.executable.size),
        result.executable.sha256, result.sdl.version,
        static_cast<unsigned long long>(result.sdl.size), result.sdl.sha256);
  }
  return result;
}

FileIdentity IdentifyFile(const wchar_t* path) {
  FileIdentity result;
  if (!HashFile(path, &result)) return result;
  ReadVersion(path, result.version, sizeof(result.version));
  return result;
}

BuildIdentity IdentifyInstalledBuild(HMODULE proxy_module) {
  std::wstring executable;
  std::wstring original;
  if (!ModulePath(nullptr, &executable) ||
      !ModulePath(proxy_module, &original)) {
    BuildIdentity result;
    std::snprintf(result.reason, sizeof(result.reason),
                  "could not resolve installed paths");
    return result;
  }
  const std::size_t slash = original.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    BuildIdentity result;
    std::snprintf(result.reason, sizeof(result.reason),
                  "proxy has no install directory");
    return result;
  }
  original.resize(slash + 1);
  original.append(kOriginalSdlName);
  return IdentifyBuildFromPaths(executable.c_str(), original.c_str());
}

}  // namespace fomds
