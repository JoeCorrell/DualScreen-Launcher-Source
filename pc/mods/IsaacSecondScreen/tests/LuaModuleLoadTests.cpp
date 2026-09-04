#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

struct lua_State;

template <typename T>
T Symbol(HMODULE module, const char* name) {
  const FARPROC address = GetProcAddress(module, name);
  static_assert(sizeof(T) == sizeof(address));
  T result = nullptr;
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: LuaModuleLoadTests <Lua5.3.dll> <module.dll>\n";
    return 2;
  }
  HMODULE module = LoadLibraryA(argv[1]);
  if (!module) return 3;
  using NewState = lua_State*(__cdecl*)();
  using OpenLibs = void(__cdecl*)(lua_State*);
  using LoadBuffer = int(__cdecl*)(lua_State*, const char*, size_t,
                                    const char*, const char*);
  using PCall = int(__cdecl*)(lua_State*, int, int, int, intptr_t, void*);
  using ToString = const char*(__cdecl*)(lua_State*, int, size_t*);
  using Close = void(__cdecl*)(lua_State*);
  const auto new_state = Symbol<NewState>(module, "luaL_newstate");
  const auto open_libs = Symbol<OpenLibs>(module, "luaL_openlibs");
  const auto load_buffer = Symbol<LoadBuffer>(module, "luaL_loadbufferx");
  const auto pcall = Symbol<PCall>(module, "lua_pcallk");
  const auto to_string = Symbol<ToString>(module, "lua_tolstring");
  const auto close = Symbol<Close>(module, "lua_close");
  if (!new_state || !open_libs || !load_buffer || !pcall || !to_string ||
      !close) {
    FreeLibrary(module);
    return 4;
  }
  std::string dll = argv[2];
  std::replace(dll.begin(), dll.end(), '\\', '/');
  const size_t slash = dll.find_last_of('/');
  if (slash == std::string::npos) return 5;
  const std::string directory = dll.substr(0, slash + 1);
  const std::string script =
      "package.cpath = [[" + directory +
      "?.dll;]] .. package.cpath; "
      "assert(require('isaac_second_screen') == true)";

  lua_State* state = new_state();
  if (!state) return 6;
  open_libs(state);
  int result = load_buffer(state, script.data(), script.size(),
                           "@LuaModuleLoadTests", nullptr);
  if (result == 0) result = pcall(state, 0, 0, 0, 0, nullptr);
  if (result != 0) {
    const char* error = to_string(state, -1, nullptr);
    std::cerr << (error ? error : "Lua module load failed") << '\n';
  }
  close(state);
  FreeLibrary(module);
  if (result != 0) return 7;
  std::cout << "LuaModuleLoadTests passed\n";
  return 0;
}
