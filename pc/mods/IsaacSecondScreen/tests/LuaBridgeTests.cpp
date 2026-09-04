#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

struct lua_State;

template <typename T>
T Symbol(HMODULE module, const char* name) {
  const FARPROC address = GetProcAddress(module, name);
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address));
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: LuaBridgeTests <Lua5.3.dll> <winmm.dll>\n";
    return 2;
  }
  HMODULE lua = LoadLibraryA(argv[1]);
  if (!lua) return 3;

  using NewState = lua_State*(__cdecl*)();
  using OpenLibs = void(__cdecl*)(lua_State*);
  using LoadString = int(__cdecl*)(lua_State*, const char*);
  using PCall = int(__cdecl*)(lua_State*, int, int, int, intptr_t, void*);
  using ToString = const char*(__cdecl*)(lua_State*, int, size_t*);
  using Close = void(__cdecl*)(lua_State*);
  const NewState new_state = Symbol<NewState>(lua, "luaL_newstate");
  const OpenLibs open_libs = Symbol<OpenLibs>(lua, "luaL_openlibs");
  const LoadString load_string = Symbol<LoadString>(lua, "luaL_loadstring");
  const PCall pcall = Symbol<PCall>(lua, "lua_pcallk");
  const ToString to_string = Symbol<ToString>(lua, "lua_tolstring");
  const Close close = Symbol<Close>(lua, "lua_close");
  if (!new_state || !open_libs || !load_string || !pcall || !to_string ||
      !close) {
    return 4;
  }

  lua_State* state = new_state();
  if (!state) return 5;
  open_libs(state);
  const std::string script =
      "local loader, err = package.loadlib([[" + std::string(argv[2]) +
      "]], 'luaopen_isaac_second_screen')\n"
      "assert(type(loader) == 'function', err)\n"
      "local bridge = loader()\n"
      "assert(type(bridge) == 'table')\n"
      "assert(type(bridge.send) == 'function')\n"
      "assert(bridge.send('probe') == false)\n";
  int result = load_string(state, script.c_str());
  if (result == 0) result = pcall(state, 0, 0, 0, 0, nullptr);
  if (result != 0) {
    const char* error = to_string(state, -1, nullptr);
    std::cerr << (error ? error : "Lua bridge invocation failed") << '\n';
  }
  close(state);
  FreeLibrary(lua);
  if (result != 0) return 6;
  std::cout << "LuaBridgeTests passed\n";
  return 0;
}
