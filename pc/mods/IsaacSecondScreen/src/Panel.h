#pragma once

#include <windows.h>

#include <cstddef>

namespace isaacds {

bool StartPanel(HMODULE module, const char* build_label);
void StopPanel();
bool SubmitStatePacket(const char* packet, size_t bytes);

}  // namespace isaacds
