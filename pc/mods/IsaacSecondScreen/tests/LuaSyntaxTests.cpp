#include <windows.h>

#include <cstring>
#include <iostream>

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
    std::cerr << "usage: LuaSyntaxTests <Lua5.3.dll> <main.lua>\n";
    return 2;
  }
  HMODULE module = LoadLibraryA(argv[1]);
  if (!module) {
    std::cerr << "could not load exact game Lua runtime\n";
    return 3;
  }
  using NewState = lua_State*(__cdecl*)();
  using LoadFile = int(__cdecl*)(lua_State*, const char*, const char*);
  using ToString = const char*(__cdecl*)(lua_State*, int, size_t*);
  using Close = void(__cdecl*)(lua_State*);
  const auto new_state = Symbol<NewState>(module, "luaL_newstate");
  const auto load_file = Symbol<LoadFile>(module, "luaL_loadfilex");
  const auto to_string = Symbol<ToString>(module, "lua_tolstring");
  const auto close = Symbol<Close>(module, "lua_close");
  if (!new_state || !load_file || !to_string || !close) {
    std::cerr << "exact game Lua runtime exports are incomplete\n";
    FreeLibrary(module);
    return 4;
  }
  lua_State* state = new_state();
  if (!state) {
    FreeLibrary(module);
    return 5;
  }
  const int result = load_file(state, argv[2], nullptr);
  if (result != 0) {
    const char* error = to_string(state, -1, nullptr);
    std::cerr << (error ? error : "Lua syntax load failed") << '\n';
  }
  close(state);
  FreeLibrary(module);
  if (result != 0) return 6;
  std::cout << "LuaSyntaxTests passed\n";
  return 0;
}
