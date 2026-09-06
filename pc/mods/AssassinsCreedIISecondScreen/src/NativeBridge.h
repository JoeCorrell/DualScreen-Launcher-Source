#pragma once
#include "State.h"
namespace ac2ds {
using NativeLogger = void(*)(const char*, ...);
// Call only after the executable's full SHA256 has been accepted.
bool InstallNativeBridge(StateStore& store, NativeLogger logger);
}
