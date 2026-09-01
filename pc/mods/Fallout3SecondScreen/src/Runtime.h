#pragma once

#include <windows.h>

namespace fo3ds {

bool StartRuntime(HMODULE module);
void NotifyProcessDetach();

}  // namespace fo3ds
