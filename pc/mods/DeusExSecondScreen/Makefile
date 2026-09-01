# Build from a real MSYS2 shell:  make
#
# Do not drive this from Git Bash. Two MSYS runtimes in one process tree makes
# the compiler exit 1 with nothing on stderr, which looks exactly like a source
# error and is not one. Use build.ps1 from PowerShell instead.

CXX      := i686-w64-mingw32-g++
SRC      := $(wildcard src/*.cpp)
OBJ      := $(patsubst src/%.cpp,build/%.o,$(SRC))
DEP      := $(OBJ:.o=.d)
TARGET   := build/dxgi.dll

CXXFLAGS := -m32 -std=c++17 -O2 -fno-exceptions -fno-rtti \
            -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type \
            -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601

LDFLAGS  := -m32 -shared -static -static-libgcc -static-libstdc++ \
            -Wl,--kill-at -Wl,--enable-stdcall-fixup

LIBS     := -ld3d11 -ldxgi -lgdi32 -luser32 -lole32 -luuid -lws2_32 -lz

.PHONY: all clean validate
all: $(TARGET) validate

build:
	@mkdir -p build

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TARGET): $(OBJ) src/dxgi.def
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) src/dxgi.def $(LIBS)
	@echo "built $@"

validate: $(TARGET)
	powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 -ValidateOnly

clean:
	rm -rf build

-include $(DEP)
