#include "BuildIdentity.h"

#include <bcrypt.h>

#include <cstdio>
#include <cwchar>
#include <cstring>
#include <vector>

namespace gdds {
namespace {

constexpr char kExeHash[] =
    "aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8";
constexpr char kGameHash[] =
    "fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f";
constexpr char kEngineHash[] =
    "687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580";
constexpr uint64_t kExeSize = 3812864;
constexpr uint64_t kGameSize = 8871936;
constexpr uint64_t kEngineSize = 3668480;
constexpr uint32_t kExeTimestamp = 0x6a7f6a16;

bool HashFile(const wchar_t* path, FileIdentity* output) {
  if (!path || !output) return false;
  *output = {};
  HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
    CloseHandle(file);
    return false;
  }

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD result_size = 0;
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                         nullptr, 0) >= 0 &&
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&object_size),
                              sizeof(object_size), &result_size, 0) >= 0;
  std::vector<UCHAR> object(ok ? object_size : 0);
  if (ok)
    ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                          nullptr, 0, 0) >= 0;
  std::vector<UCHAR> buffer(64 * 1024);
  while (ok) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr)) {
      ok = false;
      break;
    }
    if (!read) break;
    if (BCryptHashData(hash, buffer.data(), read, 0) < 0) ok = false;
  }
  UCHAR digest[32] = {};
  if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  CloseHandle(file);
  if (!ok) return false;
  output->present = true;
  output->size = static_cast<uint64_t>(size.QuadPart);
  for (size_t index = 0; index < sizeof(digest); ++index)
    std::snprintf(output->sha256 + index * 2, 3, "%02x", digest[index]);
  return true;
}

bool ModulePath(HMODULE module, wchar_t* output) {
  if (!module || !output) return false;
  const DWORD length = GetModuleFileNameW(module, output, MAX_PATH);
  return length != 0 && length < MAX_PATH;
}

bool Join(const wchar_t* directory, const wchar_t* name, wchar_t* output) {
  if (!directory || !name || !output) return false;
  const int written = std::swprintf(output, MAX_PATH, L"%ls\\%ls", directory,
                                    name);
  return written > 0 && written < MAX_PATH;
}

BuildIdentity IdentifyBuild(HMODULE executable_module, HMODULE game_module,
                            HMODULE engine_module, bool require_loaded) {
  BuildIdentity identity;
  std::snprintf(identity.label, sizeof(identity.label), "unsupported");
  wchar_t executable_path[MAX_PATH] = {};
  if (!ModulePath(executable_module, executable_path)) return identity;
  std::wcsncpy(identity.install_directory, executable_path, MAX_PATH - 1);
  wchar_t* slash = wcsrchr(identity.install_directory, L'\\');
  if (!slash) return identity;
  *slash = L'\0';

  if (executable_module) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable_module);
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
      const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
          reinterpret_cast<const unsigned char*>(executable_module) +
          dos->e_lfanew);
      identity.valid_pe = nt->Signature == IMAGE_NT_SIGNATURE &&
                          nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
      identity.is_i386 = identity.valid_pe &&
                         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
      identity.timestamp = nt->FileHeader.TimeDateStamp;
    }
  }

  wchar_t game_path[MAX_PATH] = {};
  wchar_t engine_path[MAX_PATH] = {};
  bool have_game_path = ModulePath(game_module, game_path);
  bool have_engine_path = ModulePath(engine_module, engine_path);
  if (!require_loaded) {
    if (!have_game_path)
      have_game_path = Join(identity.install_directory, L"Game.dll", game_path);
    if (!have_engine_path)
      have_engine_path = Join(identity.install_directory, L"Engine.dll", engine_path);
  }
  HashFile(executable_path, &identity.executable);
  if (have_game_path) HashFile(game_path, &identity.game);
  if (have_engine_path) HashFile(engine_path, &identity.engine);
  identity.supported =
      identity.valid_pe && identity.is_i386 &&
      identity.timestamp == kExeTimestamp &&
      identity.executable.size == kExeSize && identity.game.size == kGameSize &&
      identity.engine.size == kEngineSize &&
      std::strcmp(identity.executable.sha256, kExeHash) == 0 &&
      std::strcmp(identity.game.sha256, kGameHash) == 0 &&
      std::strcmp(identity.engine.sha256, kEngineHash) == 0;
  if (identity.supported)
    std::snprintf(identity.label, sizeof(identity.label),
                  "Grim Dawn 1.3.0.7 I386");
  return identity;
}

}  // namespace

BuildIdentity IdentifyInstalledBuild() {
  return IdentifyBuild(GetModuleHandleW(nullptr), GetModuleHandleW(L"Game.dll"),
                       GetModuleHandleW(L"Engine.dll"), false);
}

BuildIdentity IdentifyLoadedBuild(HMODULE executable, HMODULE game,
                                  HMODULE engine) {
  if (!executable || !game || !engine) return {};
  return IdentifyBuild(executable, game, engine, true);
}

}  // namespace gdds
