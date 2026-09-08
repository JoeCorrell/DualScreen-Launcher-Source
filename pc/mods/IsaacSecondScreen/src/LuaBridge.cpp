#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "LuaBridge.h"
#include "Log.h"
#include "Panel.h"
#include "NativeState.h"
#include "NativeMap.h"

struct lua_State;

namespace {

using LuaCFunction = int(__cdecl*)(lua_State*);
using LuaCheckLString = const char*(__cdecl*)(lua_State*, int, size_t*);
using LuaCreateTable = void(__cdecl*)(lua_State*, int, int);
using LuaPushBoolean = void(__cdecl*)(lua_State*, int);
using LuaPushCClosure = void(__cdecl*)(lua_State*, LuaCFunction, int);
using LuaSetField = void(__cdecl*)(lua_State*, int, const char*);
using LuaSetGlobal = void(__cdecl*)(lua_State*, const char*);
using LuaLoadFileX = int(__cdecl*)(lua_State*, const char*, const char*);
using LuaPushLString = const char*(__cdecl*)(lua_State*, const char*, size_t);
using LuaCheckInteger = int64_t(__cdecl*)(lua_State*, int);
using LuaPushInteger = void(__cdecl*)(lua_State*, int64_t);

LuaCheckLString g_check_lstring = nullptr;
LuaCreateTable g_create_table = nullptr;
LuaPushBoolean g_push_boolean = nullptr;
LuaPushCClosure g_push_cclosure = nullptr;
LuaSetField g_set_field = nullptr;
LuaSetGlobal g_set_global = nullptr;
LuaLoadFileX g_real_loadfilex = nullptr;
LuaPushLString g_push_lstring = nullptr;
LuaCheckInteger g_check_integer = nullptr;
LuaPushInteger g_push_integer = nullptr;
INIT_ONCE g_lua_once = INIT_ONCE_STATIC_INIT;
volatile LONG g_hook_installed = 0;
volatile LONG g_global_logged = 0;

template <typename T>
T LuaSymbol(HMODULE module, const char* name) {
  const FARPROC address = module ? GetProcAddress(module, name) : nullptr;
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address),
                "Windows function pointers must have equal width");
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

BOOL CALLBACK InitializeLuaApi(PINIT_ONCE, PVOID, PVOID*) {
  HMODULE lua = GetModuleHandleW(L"Lua5.3.3r.dll");
  if (!lua) lua = GetModuleHandleW(L"lua5.3.dll");
  g_check_lstring = LuaSymbol<LuaCheckLString>(lua, "luaL_checklstring");
  g_create_table = LuaSymbol<LuaCreateTable>(lua, "lua_createtable");
  g_push_boolean = LuaSymbol<LuaPushBoolean>(lua, "lua_pushboolean");
  g_push_cclosure = LuaSymbol<LuaPushCClosure>(lua, "lua_pushcclosure");
  g_set_field = LuaSymbol<LuaSetField>(lua, "lua_setfield");
  g_set_global = LuaSymbol<LuaSetGlobal>(lua, "lua_setglobal");
  g_push_lstring = LuaSymbol<LuaPushLString>(lua, "lua_pushlstring");
  g_check_integer = LuaSymbol<LuaCheckInteger>(lua, "luaL_checkinteger");
  g_push_integer = LuaSymbol<LuaPushInteger>(lua, "lua_pushinteger");
  return g_check_lstring && g_create_table && g_push_boolean &&
         g_push_cclosure && g_set_field && g_set_global && g_push_lstring && g_check_integer && g_push_integer;
}

bool EnsureLuaApi() {
  return InitOnceExecuteOnce(&g_lua_once, InitializeLuaApi, nullptr, nullptr) !=
             FALSE &&
         g_check_lstring && g_create_table && g_push_boolean &&
         g_push_cclosure && g_set_field && g_set_global && g_push_lstring && g_check_integer && g_push_integer;
}

int __cdecl SendState(lua_State* state) {
  size_t bytes = 0;
  const char* packet = g_check_lstring(state, 1, &bytes);
  const bool accepted = isaacds::SubmitStatePacket(packet, bytes);
  g_push_boolean(state, accepted ? 1 : 0);
  return 1;
}

int __cdecl NativeRunState(lua_State* state) {
  const auto snapshot = isaacds::ReadNativeRunState();
  g_push_lstring(state, snapshot.data(), snapshot.size());
  return 1;
}

int __cdecl NativeRoomFlags(lua_State* state) {
  const auto snapshot = isaacds::ReadNativeRoomFlags();
  g_push_lstring(state, snapshot.data(), snapshot.size());
  return 1;
}

int __cdecl NativeHits(lua_State* state) {
  const int64_t restore=g_check_integer(state,1);
  const int value=restore>=-1 && restore<=100000000 ?
      isaacds::NativeHitCount(static_cast<int>(restore)) : -1;
  g_push_integer(state,value);
  return 1;
}

int __cdecl NativeMap(lua_State* state) {
  const std::string result=isaacds::ReadNativeMapCommands();
  g_push_lstring(state,result.data(),result.size());
  return 1;
}

void PublishSendGlobal(lua_State* state) {
  if (!state || !EnsureLuaApi()) return;
  g_push_cclosure(state, SendState, 0);
  g_set_global(state, "IsaacSecondScreenSend");
  g_push_cclosure(state, NativeRunState, 0);
  g_set_global(state, "IsaacSecondScreenNativeState");
  g_push_cclosure(state, NativeRoomFlags, 0);
  g_set_global(state, "IsaacSecondScreenRoomFlags");
  g_push_cclosure(state, NativeHits, 0);
  g_set_global(state, "IsaacSecondScreenHits");
  g_push_cclosure(state, NativeMap, 0);
  g_set_global(state, "IsaacSecondScreenNativeMap");
  if (InterlockedCompareExchange(&g_global_logged, 1, 0) == 0) {
    isaacds::Log(isaacds::LogLevel::kInfo,
                 "[LuaBridge] native state-submit global published");
  }
}

int __cdecl HookLuaLoadFileX(lua_State* state, const char* filename,
                             const char* mode) {
  isaacds::InstallCompanionAchievementCompatibility();
  isaacds::InstallNativeMapObserver();
  PublishSendGlobal(state);
  return g_real_loadfilex ? g_real_loadfilex(state, filename, mode) : 1;
}

bool EqualAsciiInsensitive(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    char a = *left++;
    char b = *right++;
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;
  }
  return *left == *right;
}

}  // namespace

namespace isaacds {

bool InstallLuaBridgeHook(HMODULE executable) {
  if (InterlockedCompareExchange(&g_hook_installed, 0, 0) != 0) return true;
  if (!executable || !EnsureLuaApi()) return false;

  auto* base = reinterpret_cast<unsigned char*>(executable);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + static_cast<size_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    return false;
  const IMAGE_DATA_DIRECTORY& imports =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!imports.VirtualAddress ||
      imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
    return false;

  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      base + imports.VirtualAddress);
  for (; descriptor->Name; ++descriptor) {
    const char* library =
        reinterpret_cast<const char*>(base + descriptor->Name);
    if (!EqualAsciiInsensitive(library, "Lua5.3.3r.dll") &&
        !EqualAsciiInsensitive(library, "lua5.3.dll"))
      continue;
    if (!descriptor->FirstThunk) return false;
    auto* iat = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        base + descriptor->FirstThunk);
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        base + (descriptor->OriginalFirstThunk
                    ? descriptor->OriginalFirstThunk
                    : descriptor->FirstThunk));
    for (size_t index = 0; names[index].u1.AddressOfData; ++index) {
      if (IMAGE_SNAP_BY_ORDINAL32(names[index].u1.Ordinal)) continue;
      const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
          base + names[index].u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(imported->Name),
                      "luaL_loadfilex") != 0)
        continue;

      static_assert(sizeof(LuaLoadFileX) == sizeof(uint32_t),
                    "The Isaac bridge must remain a PE32 patch");
      uint32_t original_value = iat[index].u1.Function;
      std::memcpy(&g_real_loadfilex, &original_value, sizeof(original_value));
      LuaLoadFileX hook = HookLuaLoadFileX;
      uint32_t hook_value = 0;
      std::memcpy(&hook_value, &hook, sizeof(hook_value));
      DWORD previous = 0;
      if (!VirtualProtect(&iat[index].u1.Function,
                          sizeof(iat[index].u1.Function), PAGE_READWRITE,
                          &previous)) {
        g_real_loadfilex = nullptr;
        return false;
      }
      iat[index].u1.Function = hook_value;
      DWORD ignored = 0;
      VirtualProtect(&iat[index].u1.Function, sizeof(iat[index].u1.Function),
                     previous, &ignored);
      FlushInstructionCache(GetCurrentProcess(), &iat[index].u1.Function,
                            sizeof(iat[index].u1.Function));
      InterlockedExchange(&g_hook_installed, 1);
      Log(LogLevel::kInfo,
          "[LuaBridge] exact J460 luaL_loadfilex import hooked");
      return true;
    }
    return false;
  }
  return false;
}

}  // namespace isaacds

extern "C" __declspec(dllexport) int __cdecl
luaopen_isaac_second_screen(lua_State* state) {
  if (!state || !EnsureLuaApi()) {
    isaacds::Log(isaacds::LogLevel::kError,
                 "[LuaBridge] exact Lua 5.3 API exports unavailable");
    return 0;
  }
  g_create_table(state, 0, 1);
  g_push_cclosure(state, SendState, 0);
  g_set_field(state, -2, "send");
  isaacds::Log(isaacds::LogLevel::kInfo,
               "[LuaBridge] direct in-process state API opened");
  return 1;
}
